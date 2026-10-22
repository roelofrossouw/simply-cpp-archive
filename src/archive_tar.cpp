#include "archive_tar.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace sc::archive::format {
    namespace {
        constexpr std::size_t block = 512;

        // Header fields: offset and length.
        struct field {
            std::size_t at, length;
        };
        constexpr field name_field{0, 100}, mode_field{100, 8}, uid_field{108, 8}, gid_field{116, 8}, size_field{124, 12},
                mtime_field{136, 12}, checksum_field{148, 8}, magic_field{257, 6}, version_field{263, 2}, prefix_field{345, 155};
        constexpr std::size_t type_at = 156;

        std::size_t padded(const std::uint64_t size) { return static_cast<std::size_t>((size + block - 1) / block * block); }

        std::string_view part(const std::string_view header, const field f) { return header.substr(f.at, f.length); }

        std::string text(const std::string_view bytes) { return std::string{bytes.substr(0, bytes.find('\0'))}; }

        // Octal, space- or NUL-ended; or base-256 (the high bit set), as GNU tar writes large values.
        std::uint64_t number(const std::string_view bytes) {
            std::uint64_t value = 0;
            if (!bytes.empty() && (static_cast<unsigned char>(bytes[0]) & 0x80)) {
                value = static_cast<unsigned char>(bytes[0]) & 0x7f;
                for (const char c: bytes.substr(1)) value = value << 8 | static_cast<unsigned char>(c);
                return value;
            }
            bool digits = false;
            for (const char c: bytes) {
                if (c == ' ' || c == '\0') {
                    if (digits) break;
                    continue;
                }
                if (c < '0' || c > '7') throw std::runtime_error("tar: a header number isn't octal");
                value = value * 8 + static_cast<std::uint64_t>(c - '0');
                digits = true;
            }
            return value;
        }

        // The checksum counts the header with its own field as spaces; old tars summed signed bytes.
        bool checksum_right(const std::string_view header) {
            std::uint64_t stored;
            try {
                stored = number(part(header, checksum_field));
            } catch (const std::runtime_error &) {
                return false;
            }
            std::uint64_t unsigned_sum = 0;
            std::int64_t signed_sum = 0;
            for (std::size_t i = 0; i < block; ++i) {
                const bool in_field = i >= checksum_field.at && i < checksum_field.at + checksum_field.length;
                const char c = in_field ? ' ' : header[i];
                unsigned_sum += static_cast<unsigned char>(c);
                signed_sum += static_cast<signed char>(c);
            }
            return stored == unsigned_sum || static_cast<std::int64_t>(stored) == signed_sum;
        }

        bool zeros(const std::string_view header) { return std::ranges::all_of(header, [](const char c) { return c == '\0'; }); }

        // pax records: "<length> <key>=<value>\n", the length counting the whole record.
        std::map<std::string, std::string> pax_records(std::string_view data) {
            std::map<std::string, std::string> records;
            while (!data.empty()) {
                const auto space = data.find(' ');
                if (space == std::string_view::npos) break;
                std::size_t length = 0;
                for (const char c: data.substr(0, space)) {
                    if (c < '0' || c > '9') throw std::runtime_error("tar: a bad pax record");
                    length = length * 10 + static_cast<std::size_t>(c - '0');
                }
                if (length <= space + 1 || length > data.size()) throw std::runtime_error("tar: a bad pax record");
                const auto record = data.substr(space + 1, length - space - 2); // without the '\n'
                if (const auto equals = record.find('='); equals != std::string_view::npos) {
                    records[std::string{record.substr(0, equals)}] = std::string{record.substr(equals + 1)};
                }
                data.remove_prefix(length);
            }
            return records;
        }

        std::string slurp(const std::filesystem::path &file, const std::string &what) {
            std::ifstream in{file, std::ios::binary};
            if (!in) throw std::runtime_error("tar: can't read " + file.string() + what);
            std::stringstream all;
            all << in.rdbuf();
            return all.str();
        }

        void octal(std::string &header, const field f, const std::uint64_t value) {
            char digits[32];
            std::snprintf(digits, sizeof digits, "%0*llo", static_cast<int>(f.length - 1), static_cast<unsigned long long>(value));
            header.replace(f.at, f.length - 1, digits, f.length - 1);
        }

        bool fits(const field f, const std::uint64_t value) { return value < (std::uint64_t{1} << (3 * (f.length - 1))); }

        void put(std::string &header, const field f, const std::string_view value) { header.replace(f.at, value.size(), value); }

        std::string header(const std::string_view name, const std::string_view prefix, const std::uint64_t size,
                           const std::int64_t mtime, const char type) {
            std::string out(block, '\0');
            put(out, name_field, name);
            put(out, prefix_field, prefix);
            octal(out, mode_field, type == '5' ? 0755 : 0644);
            octal(out, uid_field, 0);
            octal(out, gid_field, 0);
            octal(out, size_field, fits(size_field, size) ? size : 0); // a pax size then says it
            octal(out, mtime_field, mtime > 0 ? static_cast<std::uint64_t>(mtime) : 0);
            out[type_at] = type;
            put(out, magic_field, std::string_view{"ustar\0", 6});
            put(out, version_field, "00");
            out.replace(checksum_field.at, checksum_field.length, checksum_field.length, ' ');
            unsigned sum = 0;
            for (const char c: out) sum += static_cast<unsigned char>(c);
            char digits[8];
            std::snprintf(digits, sizeof digits, "%06o", sum);
            out.replace(checksum_field.at, 7, digits, 7); // six digits, a NUL, then the space left
            return out;
        }

        std::string pax_record(const std::string &key, const std::string &value) {
            const auto body = " " + key + "=" + value + "\n";
            auto length = body.size() + 1;
            while (std::to_string(length).size() + body.size() != length) length = std::to_string(length).size() + body.size();
            return std::to_string(length) + body;
        }

        void pad(std::string &out) { out.append(padded(out.size()) - out.size(), '\0'); }
    }

    struct tar_reader::implementation {
        std::string bytes;
        std::vector<entry> entries;
        std::vector<std::pair<std::size_t, std::size_t>> data; // each entry's offset and size in bytes
        std::unordered_map<std::string, std::size_t> index;

        void list() {
            const std::string_view all{bytes};
            std::string long_name;
            std::map<std::string, std::string> pax;
            for (std::size_t at = 0; at + block <= all.size();) {
                const auto head = all.substr(at, block);
                if (zeros(head)) break;
                if (!checksum_right(head)) throw std::runtime_error("tar: a header's checksum is wrong (corrupt, or not tar)");
                const char type = head[type_at];
                const bool extension = type == 'x' || type == 'g' || type == 'L' || type == 'K';
                std::uint64_t size = number(part(head, size_field));
                if (!extension && pax.contains("size")) size = std::stoull(pax["size"]);
                const auto start = at + block;
                if (size > all.size() - start) throw std::runtime_error("tar: the archive is cut short");
                const auto body = all.substr(start, static_cast<std::size_t>(size));
                at = start + padded(size);
                if (extension) {
                    if (type == 'x') pax = pax_records(body);
                    if (type == 'L') long_name = text(body);
                    continue;
                }

                std::string name;
                if (pax.contains("path")) name = pax["path"];
                else if (!long_name.empty()) name = long_name;
                else {
                    name = text(part(head, name_field));
                    // POSIX ustar only: GNU's "ustar  " keeps other things there.
                    if (part(head, magic_field) == std::string_view{"ustar\0", 6}) {
                        if (const auto prefix = text(part(head, prefix_field)); !prefix.empty()) name = prefix + "/" + name;
                    }
                }
                const auto mtime = pax.contains("mtime") ? static_cast<std::int64_t>(std::stod(pax["mtime"]))
                                                         : static_cast<std::int64_t>(number(part(head, mtime_field)));
                long_name.clear();
                pax.clear();

                const bool folder = type == '5';
                if (!folder && type != '0' && type != '\0' && type != '7') continue; // links, devices, FIFOs
                while (name.starts_with("./")) name.erase(0, 2);
                if (folder && !name.empty() && !name.ends_with('/')) name += '/';
                if (name.empty()) continue;

                entry item;
                item.name = name;
                item.size = folder ? 0 : size;
                item.compressed_size = item.size;
                item.modified = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(mtime));
                item.folder = folder;
                const std::pair<std::size_t, std::size_t> where{start, folder ? 0 : static_cast<std::size_t>(size)};
                if (const auto found = index.find(name); found != index.end()) {
                    entries[found->second] = std::move(item);
                    data[found->second] = where;
                } else {
                    index.emplace(name, entries.size());
                    entries.push_back(std::move(item));
                    data.push_back(where);
                }
            }
        }
    };

    tar_reader::tar_reader(const std::filesystem::path &file) : tar_reader(slurp(file, "")) {}

    tar_reader::tar_reader(std::string bytes) : implementation_(std::make_unique<implementation>()) {
        implementation_->bytes = std::move(bytes);
        if (!recognizes(implementation_->bytes)) throw std::runtime_error("tar: not a tar archive");
        try {
            implementation_->list();
        } catch (const std::logic_error &error) { // a pax number std::stoull or std::stod can't read
            throw std::runtime_error(std::string{"tar: a bad pax header ("} + error.what() + ")");
        }
    }

    tar_reader::~tar_reader() = default;

    bool tar_reader::recognizes(const std::string_view bytes) {
        return bytes.size() >= block && !zeros(bytes.substr(0, block)) && checksum_right(bytes.substr(0, block));
    }

    const std::vector<entry> &tar_reader::entries() const { return implementation_->entries; }

    std::string tar_reader::read(const std::string_view name) const {
        const auto found = implementation_->index.find(std::string{name});
        if (found == implementation_->index.end()) throw std::out_of_range("tar: no entry " + std::string{name});
        const auto [offset, size] = implementation_->data[found->second];
        return implementation_->bytes.substr(offset, size);
    }

    struct tar_writer::implementation {
        struct item {
            std::string name;
            std::string bytes;
            std::filesystem::path file; // read at close, when not empty
            std::chrono::system_clock::time_point modified;
        };

        std::filesystem::path file;
        std::shared_ptr<const compression::codec> around;
        std::vector<item> items;
        bool closed = false;

        void check(const std::string_view name, const std::optional<method> how) const {
            if (closed) throw std::logic_error("tar: the archive is closed");
            if (name.empty()) throw std::invalid_argument("tar: an entry needs a name");
            if (how && *how != method::store) {
                throw std::invalid_argument("tar: entries are stored; compress the whole archive with a codec instead");
            }
        }

        void keep(item added) {
            const auto same = std::ranges::find(items, added.name, &item::name);
            if (same != items.end()) *same = std::move(added);
            else items.push_back(std::move(added));
        }

        // A pax header first when ustar can't hold the name (100 bytes, or a 155-byte prefix and
        // a 100-byte rest split at a '/') or the size (8 GiB).
        static void put_entry(std::string &out, const std::string &name, const std::string_view bytes,
                              const std::chrono::system_clock::time_point modified) {
            const bool folder = name.ends_with('/');
            const auto mtime = std::chrono::duration_cast<std::chrono::seconds>(modified.time_since_epoch()).count();
            std::string short_name = name, prefix;
            bool long_name = false;
            if (name.size() > name_field.length) {
                const auto split = name.find_last_of('/', name.size() - (folder ? 2 : 1));
                if (split != std::string::npos && split <= prefix_field.length && name.size() - split - 1 <= name_field.length) {
                    prefix = name.substr(0, split);
                    short_name = name.substr(split + 1);
                } else {
                    long_name = true;
                    short_name = name.substr(name.size() - name_field.length);
                }
            }
            std::string records;
            if (long_name) records += pax_record("path", name);
            if (!fits(size_field, bytes.size())) records += pax_record("size", std::to_string(bytes.size()));
            if (!records.empty()) {
                out += header("PaxHeader/" + short_name.substr(0, name_field.length - 10), {}, records.size(), mtime, 'x');
                out += records;
                pad(out);
            }
            out += header(short_name, prefix, folder ? 0 : bytes.size(), mtime, folder ? '5' : '0');
            if (!folder) {
                out += bytes;
                pad(out);
            }
        }

        void write() {
            std::string out;
            for (const auto &added: items) {
                if (added.file.empty()) put_entry(out, added.name, added.bytes, added.modified);
                else {
                    const auto file_time = std::filesystem::last_write_time(added.file);
                    const auto modified = std::chrono::system_clock::now() +
                                          std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                                  file_time - std::filesystem::file_time_type::clock::now());
                    put_entry(out, added.name, slurp(added.file, " for " + added.name), modified);
                }
            }
            out.append(2 * block, '\0');
            if (around) out = around->compress(out);
            auto part = file;
            part += ".part";
            {
                std::ofstream stream{part, std::ios::binary | std::ios::trunc};
                stream << out;
                if (!stream) {
                    std::filesystem::remove(part);
                    throw std::runtime_error("tar: can't write " + file.string());
                }
            }
            std::filesystem::rename(part, file);
        }
    };

    tar_writer::tar_writer(const std::filesystem::path &file, std::shared_ptr<const compression::codec> around)
        : implementation_(std::make_unique<implementation>()) {
        implementation_->file = file;
        implementation_->around = std::move(around);
    }

    tar_writer::~tar_writer() {
        if (!implementation_ || implementation_->closed) return;
        try {
            close();
        } catch (const std::exception &error) {
            std::cerr << error.what() << std::endl;
        }
    }

    void tar_writer::add(const std::string_view name, std::string bytes, const std::optional<method> how) {
        implementation_->check(name, how);
        implementation_->keep({std::string{name}, std::move(bytes), {}, std::chrono::system_clock::now()});
    }

    void tar_writer::add_file(const std::string_view name, const std::filesystem::path &file, const std::optional<method> how) {
        implementation_->check(name, how);
        if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("tar: " + file.string() + " isn't a file");
        implementation_->keep({std::string{name}, {}, file, {}});
    }

    void tar_writer::close() {
        auto &self = *implementation_;
        if (self.closed) return;
        self.closed = true;
        self.write();
        self.items.clear();
    }
}
