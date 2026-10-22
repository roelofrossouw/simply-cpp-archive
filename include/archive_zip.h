#pragma once

#include <archive_format.h>

#include <memory>

// ZIP archives (libzip), and the formats built on them: .docx, .xlsx, .pptx, .jar, .epub ...
namespace sc::archive::format {
    // Reads entries with whatever methods the system's libzip was built with. Not thread-safe.
    class zip_reader : public reader {
    public:
        // Throws std::runtime_error when the file can't be opened or isn't a ZIP archive.
        explicit zip_reader(const std::filesystem::path &file);
        // An archive held in memory.
        explicit zip_reader(std::string bytes);
        ~zip_reader() override;

        // Whether bytes start the way a ZIP archive does ("PK\3\4", or "PK\5\6" when empty).
        static bool recognizes(std::string_view bytes);

        [[nodiscard]] const std::vector<entry> &entries() const override;
        [[nodiscard]] std::string read(std::string_view name) const override;

    private:
        struct implementation;
        std::unique_ptr<implementation> implementation_;
    };

    // Writes stored or deflated entries. Not thread-safe.
    class zip_writer : public writer {
    public:
        explicit zip_writer(const std::filesystem::path &file);
        // Closes, if close() wasn't called; a failure then goes to std::cerr.
        ~zip_writer() override;

        // how: store or deflate; nullopt deflates.
        void add(std::string_view name, std::string bytes, std::optional<method> how) override;
        void add_file(std::string_view name, const std::filesystem::path &file, std::optional<method> how) override;
        void close() override;

    private:
        struct implementation;
        std::unique_ptr<implementation> implementation_;
    };
}
