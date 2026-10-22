#include "compression.h"

#include <zip.h>
#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <list>
#include <stdexcept>

namespace sc::compression {
    namespace {
        // window_bits: -15 raw deflate, 15 + 16 gzip (zlib's convention).
        std::string squeeze(const std::string_view data, const int level, const int window_bits) {
            if (level < 0 || level > 9) throw std::invalid_argument("compression level is 0 to 9");
            z_stream stream{};
            if (deflateInit2(&stream, level, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
                throw std::runtime_error("zlib: can't start compressing");
            }
            std::string out(deflateBound(&stream, static_cast<uLong>(data.size())), '\0');
            stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
            stream.avail_in = static_cast<uInt>(data.size());
            stream.next_out = reinterpret_cast<Bytef *>(out.data());
            stream.avail_out = static_cast<uInt>(out.size());
            const int result = ::deflate(&stream, Z_FINISH);
            out.resize(stream.total_out);
            deflateEnd(&stream);
            if (result != Z_STREAM_END) throw std::runtime_error("zlib: compressing failed");
            return out;
        }

        // gzip: members one after the other are read on (as gunzip does).
        std::string expand(const std::string_view data, const int window_bits, const bool members) {
            z_stream stream{};
            if (inflateInit2(&stream, window_bits) != Z_OK) throw std::runtime_error("zlib: can't start decompressing");
            std::string out;
            char buffer[64 * 1024];
            stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
            stream.avail_in = static_cast<uInt>(data.size());
            int result = Z_OK;
            while (true) {
                stream.next_out = reinterpret_cast<Bytef *>(buffer);
                stream.avail_out = sizeof buffer;
                result = ::inflate(&stream, Z_NO_FLUSH);
                out.append(buffer, sizeof buffer - stream.avail_out);
                if (result == Z_STREAM_END) {
                    if (members && stream.avail_in > 0) {
                        inflateReset(&stream);
                        continue;
                    }
                    break;
                }
                if (result != Z_OK || (stream.avail_in == 0 && stream.avail_out != 0)) break; // an error, or data cut short
            }
            const char *message = stream.msg;
            inflateEnd(&stream);
            if (result != Z_STREAM_END) {
                throw std::runtime_error(std::string{"zlib: not valid compressed data"} + (message ? std::string{" ("} + message + ")" : ""));
            }
            return out;
        }

        zip_method method_of(const zip_uint16_t method) {
            switch (method) {
                case ZIP_CM_STORE: return zip_method::store;
                case ZIP_CM_DEFLATE: return zip_method::deflate;
                case 12: return zip_method::bzip2; // ZIP_CM_BZIP2
                case 95: return zip_method::xz;    // ZIP_CM_XZ
                case 93: return zip_method::zstd;  // ZIP_CM_ZSTD
                default: return zip_method::other;
            }
        }

        // "To the end of the file": a name since libzip 1.10; older ones (jammy and noble have 1.7.3) take -1.
#ifdef ZIP_LENGTH_TO_END
        constexpr zip_int64_t whole_file = ZIP_LENGTH_TO_END;
#else
        constexpr zip_int64_t whole_file = -1;
#endif

        std::string zip_message(int code) {
            zip_error_t error;
            zip_error_init_with_code(&error, code);
            std::string text = zip_error_strerror(&error);
            zip_error_fini(&error);
            return text;
        }
    }

    std::string deflate(const std::string_view data, const int level) { return squeeze(data, level, -15); }
    std::string inflate(const std::string_view data) { return expand(data, -15, false); }
    std::string gzip(const std::string_view data, const int level) { return squeeze(data, level, 15 + 16); }
    std::string gunzip(const std::string_view data) { return expand(data, 15 + 16, true); }

    struct zip_reader::implementation {
        zip_t *archive = nullptr;
        std::string memory; // the bytes of an archive read from memory, which libzip reads in place
        std::vector<zip_entry> entries;

        ~implementation() {
            if (archive) zip_discard(archive);
        }

        void list() {
            const auto count = zip_get_num_entries(archive, 0);
            for (zip_int64_t i = 0; i < count; ++i) {
                zip_stat_t stat;
                if (zip_stat_index(archive, static_cast<zip_uint64_t>(i), 0, &stat) != 0) continue;
                zip_entry entry;
                entry.name = stat.name;
                entry.size = stat.size;
                entry.compressed_size = stat.comp_size;
                entry.method = method_of(stat.comp_method);
                entry.modified = std::chrono::system_clock::from_time_t(stat.mtime);
                entry.folder = !entry.name.empty() && entry.name.back() == '/';
                entries.push_back(std::move(entry));
            }
        }

        std::string read(const std::string_view name) const {
            const std::string key{name};
            const auto index = zip_name_locate(archive, key.c_str(), 0);
            if (index < 0) throw std::out_of_range("ZIP: no entry " + key);
            zip_stat_t stat;
            zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &stat);
            zip_file_t *file = zip_fopen_index(archive, static_cast<zip_uint64_t>(index), 0);
            if (!file) throw std::runtime_error("ZIP: can't read " + key + ": " + zip_strerror(archive));
            std::string out(stat.size, '\0');
            const auto got = zip_fread(file, out.data(), out.size());
            const std::string problem = got < 0 ? zip_file_strerror(file) : "";
            zip_fclose(file);
            if (got < 0 || static_cast<zip_uint64_t>(got) != stat.size) {
                throw std::runtime_error("ZIP: can't read " + key + (problem.empty() ? std::string{": cut short"} : ": " + problem));
            }
            return out;
        }
    };

    zip_reader::zip_reader(const std::filesystem::path &file) : implementation_(std::make_unique<implementation>()) {
        int code = 0;
        implementation_->archive = zip_open(file.string().c_str(), ZIP_RDONLY, &code);
        if (!implementation_->archive) throw std::runtime_error("ZIP: can't open " + file.string() + ": " + zip_message(code));
        implementation_->list();
    }

    zip_reader zip_reader::from_memory(std::string bytes) {
        zip_reader reader;
        reader.implementation_ = std::make_unique<implementation>();
        auto &self = *reader.implementation_;
        self.memory = std::move(bytes);
        zip_error_t error;
        zip_error_init(&error);
        zip_source_t *source = zip_source_buffer_create(self.memory.data(), self.memory.size(), 0, &error);
        if (source) self.archive = zip_open_from_source(source, ZIP_RDONLY, &error);
        if (!self.archive) {
            if (source) zip_source_free(source);
            const std::string message = zip_error_strerror(&error);
            zip_error_fini(&error);
            throw std::runtime_error("ZIP: can't open the archive: " + message);
        }
        zip_error_fini(&error);
        self.list();
        return reader;
    }

    zip_reader::~zip_reader() = default;
    zip_reader::zip_reader(zip_reader &&) noexcept = default;
    zip_reader &zip_reader::operator=(zip_reader &&) noexcept = default;

    const std::vector<zip_entry> &zip_reader::entries() const { return implementation_->entries; }

    bool zip_reader::contains(const std::string_view name) const {
        return std::ranges::any_of(implementation_->entries, [&](const zip_entry &entry) { return entry.name == name; });
    }

    std::string zip_reader::read(const std::string_view name) const { return implementation_->read(name); }

    void zip_reader::extract(const std::filesystem::path &folder, const std::string_view prefix) const {
        namespace fs = std::filesystem;
        const auto root = fs::weakly_canonical(fs::absolute(folder));
        std::vector<std::pair<const zip_entry *, fs::path>> chosen;
        for (const auto &entry: implementation_->entries) {
            if (!entry.name.starts_with(prefix)) continue;
            const auto target = fs::weakly_canonical(root / fs::path{entry.name}.relative_path());
            const auto inside = std::mismatch(root.begin(), root.end(), target.begin(), target.end()).first == root.end();
            if (fs::path{entry.name}.is_absolute() || !inside) { // "zip slip"
                throw std::runtime_error("ZIP: " + entry.name + " would land outside " + folder.string());
            }
            chosen.emplace_back(&entry, target);
        }
        for (const auto &[entry, target]: chosen) {
            if (entry->folder) {
                fs::create_directories(target);
                continue;
            }
            fs::create_directories(target.parent_path());
            std::ofstream out{target, std::ios::binary};
            out << implementation_->read(entry->name);
            if (!out) throw std::runtime_error("ZIP: can't write " + target.string());
        }
    }

    struct zip_writer::implementation {
        zip_t *archive = nullptr;
        std::filesystem::path file;
        std::list<std::string> held; // entries from memory, which libzip reads at close

        ~implementation() {
            if (!archive) return;
            if (zip_close(archive) != 0) {
                std::cerr << "ZIP: can't write " << file.string() << ": " << zip_strerror(archive) << std::endl;
                zip_discard(archive);
            }
        }

        void added(const zip_int64_t index, const std::string &name, const zip_method method) {
            if (index < 0) throw std::runtime_error("ZIP: can't add " + name + ": " + zip_strerror(archive));
            const zip_int32_t compression = method == zip_method::store ? ZIP_CM_STORE : ZIP_CM_DEFLATE;
            if (zip_set_file_compression(archive, static_cast<zip_uint64_t>(index), compression, 0) != 0) {
                throw std::runtime_error("ZIP: can't set " + name + "'s compression: " + zip_strerror(archive));
            }
        }

        void check(const std::string_view name, const zip_method method) const {
            if (!archive) throw std::logic_error("ZIP: the archive is closed");
            if (name.empty()) throw std::invalid_argument("ZIP: an entry needs a name");
            if (method != zip_method::store && method != zip_method::deflate) {
                throw std::invalid_argument("ZIP: entries are written stored or deflated");
            }
        }
    };

    zip_writer::zip_writer(const std::filesystem::path &file) : implementation_(std::make_unique<implementation>()) {
        int code = 0;
        implementation_->file = file;
        implementation_->archive = zip_open(file.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &code);
        if (!implementation_->archive) throw std::runtime_error("ZIP: can't create " + file.string() + ": " + zip_message(code));
    }

    zip_writer::~zip_writer() = default;
    zip_writer::zip_writer(zip_writer &&) noexcept = default;
    zip_writer &zip_writer::operator=(zip_writer &&) noexcept = default;

    void zip_writer::add(const std::string_view name, std::string bytes, const zip_method method) {
        auto &self = *implementation_;
        self.check(name, method);
        const auto &data = self.held.emplace_back(std::move(bytes));
        zip_source_t *source = zip_source_buffer(self.archive, data.data(), data.size(), 0);
        const std::string key{name};
        if (!source) throw std::runtime_error("ZIP: can't add " + key + ": " + zip_strerror(self.archive));
        const auto index = zip_file_add(self.archive, key.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
        if (index < 0) zip_source_free(source);
        self.added(index, key, method);
    }

    void zip_writer::add_file(const std::string_view name, const std::filesystem::path &file, const zip_method method) {
        auto &self = *implementation_;
        self.check(name, method);
        if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("ZIP: " + file.string() + " isn't a file");
        zip_source_t *source = zip_source_file(self.archive, file.string().c_str(), 0, whole_file);
        const std::string key{name};
        if (!source) throw std::runtime_error("ZIP: can't add " + file.string() + ": " + zip_strerror(self.archive));
        const auto index = zip_file_add(self.archive, key.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
        if (index < 0) zip_source_free(source);
        self.added(index, key, method);
    }

    void zip_writer::close() {
        auto &self = *implementation_;
        if (!self.archive) return;
        if (zip_close(self.archive) != 0) {
            const std::string message = zip_strerror(self.archive);
            zip_discard(self.archive);
            self.archive = nullptr;
            throw std::runtime_error("ZIP: can't write " + self.file.string() + ": " + message);
        }
        self.archive = nullptr;
        self.held.clear();
    }
}
