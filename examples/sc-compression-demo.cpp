// Lists a ZIP archive's entries (a .docx, .xlsx and .jar are ZIP archives too), or extracts them
// with --extract. Without a file, makes a small archive and a gzip stream to show both.
//
//     sc-compression-demo report.docx
//     sc-compression-demo report.docx --extract out --prefix word/media/

#include <compression.h>
#include <params.h>

#include <filesystem>
#include <iomanip>
#include <iostream>

namespace {
    const char *method_name(const sc::compression::zip_method method) {
        using sc::compression::zip_method;
        switch (method) {
            case zip_method::store: return "stored";
            case zip_method::deflate: return "deflated";
            case zip_method::bzip2: return "bzip2";
            case zip_method::xz: return "xz";
            case zip_method::zstd: return "zstd";
            default: return "other";
        }
    }

    void list(const sc::compression::zip_reader &zip) {
        for (const auto &entry: zip.entries()) {
            std::cout << std::setw(10) << entry.size << std::setw(10) << entry.compressed_size << "  " << std::setw(8) << std::left
                      << method_name(entry.method) << std::right << "  " << entry.name << '\n';
        }
        std::cout << zip.entries().size() << " entries\n";
    }
}

int main(int argc, char **argv) {
    const sc::params args{argc, argv, {
        {"name", "sc-compression-demo"},
        {"description", "Lists a ZIP archive's entries (size, compressed size, method, name), or extracts them. Without a "
                        "file, it makes a small archive and a gzip stream in the temporary folder and shows them."},
        {"options", {
            {"extract", {{"short", "x"}, {"value", "FOLDER"}, {"default", ""}, {"help", "extract into FOLDER"}}},
            {"prefix", {{"short", "p"}, {"value", "PATH"}, {"default", ""}, {"help", "only the entries starting with PATH"}}}}},
        {"positional", {
            {"file", {{"default", ""}, {"help", "the ZIP archive"}}}}}}};
    try {
        // [readme]
        namespace compression = sc::compression;
        auto file = std::filesystem::path{args.as<std::string>("file")};
        if (file.empty()) {
            file = std::filesystem::temp_directory_path() / "sc-compression-demo.zip";
            compression::zip_writer out{file};
            out.add("hello.txt", "Hello, ZIP!");
            out.add("notes/text.txt", std::string(1000, 'a'));          // deflated: 1000 bytes to a few
            out.add("notes/already.bin", "\x01\x02\x03", compression::zip_method::store);
            out.close();
            const std::string text(5000, 'x');
            std::cout << "gzip: " << text.size() << " bytes to " << compression::gzip(text).size() << ", and back to "
                      << compression::gunzip(compression::gzip(text)).size() << "\n\n";
        }
        const compression::zip_reader zip{file};
        if (const auto folder = args.as<std::string>("extract"); !folder.empty()) {
            zip.extract(folder, args.as<std::string>("prefix"));
            std::cout << "Extracted into " << folder << '\n';
        } else {
            list(zip);
        }
        // [/readme]
    } catch (const std::exception &error) {
        std::cerr << "sc-compression-demo: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
