#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// ZIP archives (libzip) and single compressed streams (zlib).
//
//     sc::compression::zip_reader docx{"letter.docx"};
//     for (const auto &entry: docx.entries()) entry.name, entry.size;
//     const std::string picture = docx.read("word/media/image1.png");
//
//     sc::compression::zip_writer out{"archive.zip"};
//     out.add("notes/readme.txt", "Hello");
//     out.add_file("photos/a.jpg", "/data/a.jpg", sc::compression::zip_method::store);
//     out.close();
//
//     const std::string packed = sc::compression::gzip(text);
//
// TODO: bzip2, xz and zstd streams (libbz2, liblzma, libzstd: all permissive, packaged everywhere).
namespace sc::compression {
    // Raw deflate (RFC 1951), as inside ZIP entries and PDF streams. level: 0 (none) to 9 (smallest),
    // 6 by default. inflate() throws std::runtime_error for data that isn't valid deflate.
    std::string deflate(std::string_view data, int level = 6);
    std::string inflate(std::string_view data);

    // gzip (RFC 1952), as in .gz files. gunzip() also reads several members one after the other, as
    // `cat a.gz b.gz` makes, and throws std::runtime_error for data that isn't gzip.
    std::string gzip(std::string_view data, int level = 6);
    std::string gunzip(std::string_view data);

    // How a ZIP entry is compressed. Reading handles the methods the system's libzip was built
    // with; writing offers store (for data already compressed, such as JPEG) and deflate.
    enum class zip_method { store, deflate, bzip2, xz, zstd, other };

    struct zip_entry {
        std::string name; // the path inside the archive, '/'-separated; a folder's ends in '/'
        std::uint64_t size = 0;
        std::uint64_t compressed_size = 0;
        zip_method method = zip_method::store;
        std::chrono::system_clock::time_point modified;
        bool folder = false;
    };

    // Reads a ZIP archive (and the formats built on it: .docx, .xlsx, .pptx, .jar, .epub ...).
    // Not thread-safe: use one per thread, or lock around it.
    class zip_reader {
    public:
        // Throws std::runtime_error when the file can't be opened or isn't a ZIP archive.
        explicit zip_reader(const std::filesystem::path &file);
        // An archive held in memory.
        static zip_reader from_memory(std::string bytes);
        ~zip_reader();
        zip_reader(zip_reader &&) noexcept;
        zip_reader &operator=(zip_reader &&) noexcept;

        // In the archive's order.
        [[nodiscard]] const std::vector<zip_entry> &entries() const;
        [[nodiscard]] bool contains(std::string_view name) const;

        // An entry's bytes; throws std::out_of_range when there's no such entry, and
        // std::runtime_error when it can't be read (corrupt, encrypted, or a method libzip lacks).
        [[nodiscard]] std::string read(std::string_view name) const;

        // Writes the entries whose names start with prefix (all of them by default) into folder,
        // keeping their paths, folders made as needed. An entry that would land outside folder
        // ("../x", an absolute path) throws std::runtime_error before anything is written.
        void extract(const std::filesystem::path &folder, std::string_view prefix = {}) const;

    private:
        zip_reader() = default;
        struct implementation;
        std::unique_ptr<implementation> implementation_;
    };

    // Writes a ZIP archive; nothing is written until close(). An existing file is replaced.
    // Not thread-safe.
    class zip_writer {
    public:
        explicit zip_writer(const std::filesystem::path &file);
        // Closes, if close() wasn't called; a failure then goes to std::cerr.
        ~zip_writer();
        zip_writer(zip_writer &&) noexcept;
        zip_writer &operator=(zip_writer &&) noexcept;

        // An entry from memory; a name added again replaces the earlier one. Throws
        // std::invalid_argument for an empty name or a method other than store and deflate.
        void add(std::string_view name, std::string bytes, zip_method method = zip_method::deflate);
        // An entry from a file, read when the archive is closed.
        void add_file(std::string_view name, const std::filesystem::path &file, zip_method method = zip_method::deflate);

        // Writes the archive; throws std::runtime_error when it can't (the file is then not made).
        void close();

    private:
        struct implementation;
        std::unique_ptr<implementation> implementation_;
    };
}
