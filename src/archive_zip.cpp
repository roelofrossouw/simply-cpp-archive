#include "archive_zip.h"

#include <zip.h>

#include <iostream>
#include <list>
#include <stdexcept>

namespace sc::archive::format {
    namespace {
        method method_of(const zip_uint16_t code) {
            switch (code) {
                case ZIP_CM_STORE: return method::store;
                case ZIP_CM_DEFLATE: return method::deflate;
                case 12: return method::bzip2; // ZIP_CM_BZIP2
                case 95: return method::xz;    // ZIP_CM_XZ
                case 93: return method::zstd;  // ZIP_CM_ZSTD
                default: return method::other;
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

    struct zip_reader::implementation {
        zip_t *archive = nullptr;
        std::string memory; // the bytes of an archive read from memory, which libzip reads in place
        std::vector<sc::archive::entry> entries;

        ~implementation() {
            if (archive) zip_discard(archive);
        }

        void list() {
            const auto count = zip_get_num_entries(archive, 0);
            for (zip_int64_t i = 0; i < count; ++i) {
                zip_stat_t stat;
                if (zip_stat_index(archive, static_cast<zip_uint64_t>(i), 0, &stat) != 0) continue;
                sc::archive::entry entry;
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

    zip_reader::zip_reader(std::string bytes) : implementation_(std::make_unique<implementation>()) {
        auto &self = *implementation_;
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
    }

    zip_reader::~zip_reader() = default;

    bool zip_reader::recognizes(const std::string_view bytes) {
        return bytes.starts_with(std::string_view{"PK\3\4", 4}) || bytes.starts_with(std::string_view{"PK\5\6", 4});
    }

    const std::vector<entry> &zip_reader::entries() const { return implementation_->entries; }

    std::string zip_reader::read(const std::string_view name) const { return implementation_->read(name); }

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

        void added(const zip_int64_t index, const std::string &name, const method how) {
            if (index < 0) throw std::runtime_error("ZIP: can't add " + name + ": " + zip_strerror(archive));
            const zip_int32_t compression = how == method::store ? ZIP_CM_STORE : ZIP_CM_DEFLATE;
            if (zip_set_file_compression(archive, static_cast<zip_uint64_t>(index), compression, 0) != 0) {
                throw std::runtime_error("ZIP: can't set " + name + "'s compression: " + zip_strerror(archive));
            }
        }

        void check(const std::string_view name, const method how) const {
            if (!archive) throw std::logic_error("ZIP: the archive is closed");
            if (name.empty()) throw std::invalid_argument("ZIP: an entry needs a name");
            if (how != method::store && how != method::deflate) {
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

    void zip_writer::add(const std::string_view name, std::string bytes, const std::optional<method> how) {
        auto &self = *implementation_;
        self.check(name, how.value_or(method::deflate));
        const auto &data = self.held.emplace_back(std::move(bytes));
        zip_source_t *source = zip_source_buffer(self.archive, data.data(), data.size(), 0);
        const std::string key{name};
        if (!source) throw std::runtime_error("ZIP: can't add " + key + ": " + zip_strerror(self.archive));
        const auto index = zip_file_add(self.archive, key.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
        if (index < 0) zip_source_free(source);
        self.added(index, key, how.value_or(method::deflate));
    }

    void zip_writer::add_file(const std::string_view name, const std::filesystem::path &file, const std::optional<method> how) {
        auto &self = *implementation_;
        self.check(name, how.value_or(method::deflate));
        if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("ZIP: " + file.string() + " isn't a file");
        zip_source_t *source = zip_source_file(self.archive, file.string().c_str(), 0, whole_file);
        const std::string key{name};
        if (!source) throw std::runtime_error("ZIP: can't add " + file.string() + ": " + zip_strerror(self.archive));
        const auto index = zip_file_add(self.archive, key.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
        if (index < 0) zip_source_free(source);
        self.added(index, key, how.value_or(method::deflate));
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
