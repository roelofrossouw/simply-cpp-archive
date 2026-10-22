#pragma once

#include <archive_format.h>
#include <codec.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Archives: many files and folders in one stream. The format (archive_zip.h, archive_tar.h) is the
// structure; a codec (codec.h) may compress the whole stream around it (a .tar.gz). This is the
// front: open() finds the format from the bytes, and create() writes one.
//
//     const auto docx = sc::archive::open("letter.docx");
//     for (const auto &entry: docx.entries()) entry.name, entry.size;
//     const std::string picture = docx.read("word/media/image1.png");
//
//     auto out = sc::archive::create("archive.zip");
//     out.add("notes/readme.txt", "Hello");
//     out.add_file("photos/a.jpg", "/data/a.jpg", sc::archive::method::store);
//     out.close();
//
//     auto tgz = sc::archive::create("backup.tar.gz", sc::archive::kind::tar, std::make_shared<sc::compression::gzip>());
namespace sc::archive {
    enum class kind { zip, tar };

    // Not thread-safe: use one per thread, or lock around it.
    class reader {
    public:
        explicit reader(std::unique_ptr<format::reader> format);

        // In the archive's order.
        [[nodiscard]] const std::vector<entry> &entries() const;
        [[nodiscard]] bool contains(std::string_view name) const;

        // An entry's bytes; throws std::out_of_range when there's no such entry, and
        // std::runtime_error when it can't be read (corrupt, encrypted, or a method that's missing).
        [[nodiscard]] std::string read(std::string_view name) const;

        // Writes the entries whose names start with prefix (all of them by default) into folder,
        // keeping their paths, folders made as needed. An entry that would land outside folder
        // ("../x", an absolute path) throws std::runtime_error before anything is written.
        void extract(const std::filesystem::path &folder, std::string_view prefix = {}) const;

    private:
        std::unique_ptr<format::reader> format_;
    };

    // Nothing is written until close(); an existing file is replaced. Not thread-safe.
    class writer {
    public:
        explicit writer(std::unique_ptr<format::writer> format);
        // Closes, if close() wasn't called; a failure then goes to std::cerr.
        ~writer();
        writer(writer &&) noexcept = default;
        writer &operator=(writer &&) noexcept = default;

        // An entry from memory; a name added again replaces the earlier one, and a name ending in
        // '/' is a folder. how: the format's own choice by default (ZIP deflates, tar stores); ZIP
        // also takes store, tar only store. Throws std::invalid_argument for an empty name or a
        // method the format can't write.
        void add(std::string_view name, std::string bytes, std::optional<method> how = std::nullopt);
        // An entry from a file, read when the archive is closed.
        void add_file(std::string_view name, const std::filesystem::path &file, std::optional<method> how = std::nullopt);

        // Writes the archive; throws std::runtime_error when it can't (the file is then not made).
        void close();

    private:
        std::unique_ptr<format::writer> format_;
    };

    // The archive in a file, its format found from its first bytes, a compressed stream (a .gz)
    // decompressed first. Throws std::runtime_error when it can't be read or isn't an archive.
    reader open(const std::filesystem::path &file);
    // The same, from an archive held in memory.
    reader from_memory(std::string bytes);

    // around: a codec for the whole archive (gzip for a .tar.gz); tar only, ZIP compresses its
    // entries instead and throws std::invalid_argument.
    writer create(const std::filesystem::path &file, kind as = kind::zip, std::shared_ptr<const compression::codec> around = {});
}
