#pragma once

#include <archive_format.h>
#include <codec.h>

#include <memory>

// tar archives (POSIX ustar and pax, reading GNU long names too), in our own code. A .tar.gz is
// tar inside the gzip codec: the reader is handed the decompressed bytes, the writer a codec.
namespace sc::archive::format {
    // Lists files and folders; links and devices are skipped. Names lose a leading "./", and a
    // name that comes again (tar appends updates) replaces the earlier entry in its place. The
    // whole archive is held in memory. Not thread-safe.
    class tar_reader : public reader {
    public:
        // Throws std::runtime_error when the file can't be read or isn't a tar archive.
        explicit tar_reader(const std::filesystem::path &file);
        explicit tar_reader(std::string bytes);
        ~tar_reader() override;

        // Whether bytes (at least the first 512) start with a tar header whose checksum is right.
        static bool recognizes(std::string_view bytes);

        [[nodiscard]] const std::vector<entry> &entries() const override;
        [[nodiscard]] std::string read(std::string_view name) const override;

    private:
        struct implementation;
        std::unique_ptr<implementation> implementation_;
    };

    // Writes ustar, with pax headers for names and sizes ustar can't hold. Entries are stored;
    // around, when given, compresses the whole archive (gzip for a .tar.gz). Not thread-safe.
    class tar_writer : public writer {
    public:
        explicit tar_writer(const std::filesystem::path &file, std::shared_ptr<const compression::codec> around = {});
        // Closes, if close() wasn't called; a failure then goes to std::cerr.
        ~tar_writer() override;

        // how: store (or nullopt); anything else throws std::invalid_argument.
        void add(std::string_view name, std::string bytes, std::optional<method> how) override;
        void add_file(std::string_view name, const std::filesystem::path &file, std::optional<method> how) override;
        void close() override;

    private:
        struct implementation;
        std::unique_ptr<implementation> implementation_;
    };
}
