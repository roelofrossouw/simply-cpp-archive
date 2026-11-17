#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What every archive format gives: a list of entries, each entry's bytes, and a way to write
// them. Each format is its own class (archive_zip.h, archive_tar.h); sc_archive.h picks one.
namespace sc::archive {
    // How an entry is compressed inside the archive. Formats that compress the whole stream
    // instead (a .tar.gz) store their entries.
    enum class method { store, deflate, bzip2, xz, zstd, other };

    struct entry {
        std::string name; // the path inside the archive, '/'-separated; a folder's ends in '/'
        std::uint64_t size = 0;
        std::uint64_t compressed_size = 0;
        archive::method method = method::store;
        std::chrono::system_clock::time_point modified;
        bool folder = false;
    };

    namespace format {
        class reader {
        public:
            virtual ~reader() = default;

            // In the archive's order.
            [[nodiscard]] virtual const std::vector<entry> &entries() const = 0;
            // An entry's bytes; throws std::out_of_range when there's no such entry, and
            // std::runtime_error when it can't be read.
            [[nodiscard]] virtual std::string read(std::string_view name) const = 0;
        };

        // Nothing need be written until close(); an existing file is replaced.
        class writer {
        public:
            virtual ~writer() = default;

            // A name added again replaces the earlier entry; a name ending in '/' is a folder.
            // how: nullopt for the format's own choice (ZIP deflates, tar stores). Throws
            // std::invalid_argument for an empty name or a method the format can't write.
            virtual void add(std::string_view name, std::string bytes, std::optional<method> how) = 0;
            // An entry from a file, which may be read only at close().
            virtual void add_file(std::string_view name, const std::filesystem::path &file, std::optional<method> how) = 0;
            // Throws std::runtime_error when the archive can't be written.
            virtual void close() = 0;
        };
    }
}
