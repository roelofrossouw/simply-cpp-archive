#include "archive.h"

#include "archive_zip.h"
#include "codec.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace sc::archive {
    namespace {
        // "PK\3\4" starts an entry, "PK\5\6" an empty archive's directory.
        bool zip_bytes(const std::string_view bytes) {
            return bytes.starts_with(std::string_view{"PK\3\4", 4}) || bytes.starts_with(std::string_view{"PK\5\6", 4});
        }

        std::string first_bytes(const std::filesystem::path &file, const std::size_t count) {
            std::ifstream in{file, std::ios::binary};
            if (!in) throw std::runtime_error("archive: can't open " + file.string());
            std::string bytes(count, '\0');
            in.read(bytes.data(), static_cast<std::streamsize>(count));
            bytes.resize(static_cast<std::size_t>(in.gcount()));
            return bytes;
        }

        std::string all_bytes(const std::filesystem::path &file) {
            std::ifstream in{file, std::ios::binary};
            std::stringstream all;
            all << in.rdbuf();
            return all.str();
        }
    }

    reader::reader(std::unique_ptr<format::reader> format) : format_(std::move(format)) {}

    const std::vector<entry> &reader::entries() const { return format_->entries(); }

    bool reader::contains(const std::string_view name) const {
        return std::ranges::any_of(format_->entries(), [&](const entry &entry) { return entry.name == name; });
    }

    std::string reader::read(const std::string_view name) const { return format_->read(name); }

    void reader::extract(const std::filesystem::path &folder, const std::string_view prefix) const {
        namespace fs = std::filesystem;
        const auto root = fs::weakly_canonical(fs::absolute(folder));
        std::vector<std::pair<const entry *, fs::path>> chosen;
        for (const auto &entry: format_->entries()) {
            if (!entry.name.starts_with(prefix)) continue;
            const auto target = fs::weakly_canonical(root / fs::path{entry.name}.relative_path());
            const auto inside = std::mismatch(root.begin(), root.end(), target.begin(), target.end()).first == root.end();
            if (fs::path{entry.name}.is_absolute() || !inside) { // "zip slip"
                throw std::runtime_error("archive: " + entry.name + " would land outside " + folder.string());
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
            out << format_->read(entry->name);
            if (!out) throw std::runtime_error("archive: can't write " + target.string());
        }
    }

    writer::writer(std::unique_ptr<format::writer> format) : format_(std::move(format)) {}

    writer::~writer() = default; // the format closes itself

    void writer::add(const std::string_view name, std::string bytes, const method how) { format_->add(name, std::move(bytes), how); }

    void writer::add_file(const std::string_view name, const std::filesystem::path &file, const method how) {
        format_->add_file(name, file, how);
    }

    void writer::close() { format_->close(); }

    reader open(const std::filesystem::path &file) {
        const auto start = first_bytes(file, 4);
        if (zip_bytes(start)) return reader{std::make_unique<format::zip_reader>(file)}; // libzip reads the file itself
        if (compression::detect(start)) return from_memory(all_bytes(file));
        throw std::runtime_error("archive: " + file.string() + " isn't an archive");
    }

    reader from_memory(std::string bytes) {
        if (zip_bytes(bytes)) return reader{std::make_unique<format::zip_reader>(std::move(bytes))};
        if (const auto codec = compression::detect(bytes)) {
            const auto inside = codec->decompress(bytes);
            if (zip_bytes(inside)) return reader{std::make_unique<format::zip_reader>(inside)};
            throw std::runtime_error("archive: " + codec->name() + " data without an archive inside");
        }
        throw std::runtime_error("archive: not an archive");
    }

    writer create(const std::filesystem::path &file, const kind as) {
        switch (as) {
            case kind::zip: return writer{std::make_unique<format::zip_writer>(file)};
        }
        throw std::invalid_argument("archive: unknown kind");
    }
}
