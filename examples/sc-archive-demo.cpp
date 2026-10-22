// Lists an archive's entries (ZIP, and .docx, .xlsx, .jar with it; tar; either compressed with
// gzip, bzip2, xz or zstd, as a .tar.gz), or extracts them with --extract. Without a file, makes a small archive and
// a gzip stream to show both.
//
//     sc-archive-demo report.docx
//     sc-archive-demo report.docx --extract out --prefix word/media/
//     sc-archive-demo backup.tar.gz

#include <sc_archive.h>
#include <codec_gzip.h>
#include <params.h>

#include <filesystem>
#include <iomanip>
#include <iostream>

namespace {
    const char *method_name(const sc::archive::method how) {
        using sc::archive::method;
        switch (how) {
            case method::store: return "stored";
            case method::deflate: return "deflated";
            case method::bzip2: return "bzip2";
            case method::xz: return "xz";
            case method::zstd: return "zstd";
            default: return "other";
        }
    }

    void list(const sc::archive::reader &zip) {
        for (const auto &entry: zip.entries()) {
            std::cout << std::setw(10) << entry.size << std::setw(10) << entry.compressed_size << "  " << std::setw(8) << std::left
                      << method_name(entry.method) << std::right << "  " << entry.name << '\n';
        }
        std::cout << zip.entries().size() << " entries\n";
    }
}

int main(int argc, char **argv) {
    const sc::params args{argc, argv, {
        {"name", "sc-archive-demo"},
        {"description", "Lists an archive's entries (size, compressed size, method, name), or extracts them. Without a "
                        "file, it makes a small archive and a gzip stream in the temporary folder and shows them."},
        {"options", {
            {"extract", {{"short", "x"}, {"value", "FOLDER"}, {"default", ""}, {"help", "extract into FOLDER"}}},
            {"prefix", {{"short", "p"}, {"value", "PATH"}, {"default", ""}, {"help", "only the entries starting with PATH"}}}}},
        {"positional", {
            {"file", {{"default", ""}, {"help", "the archive (ZIP or tar, compressed or not)"}}}}}}};
    try {
        // [readme]
        auto file = std::filesystem::path{args.as<std::string>("file")};
        if (file.empty()) {
            file = std::filesystem::temp_directory_path() / "sc-archive-demo.zip";
            auto out = sc::archive::create(file);
            out.add("hello.txt", "Hello, ZIP!");
            out.add("notes/text.txt", std::string(1000, 'a'));          // deflated: 1000 bytes to a few
            out.add("notes/already.bin", "\x01\x02\x03", sc::archive::method::store);
            out.close();
            const std::string text(5000, 'x');
            const sc::compression::gzip gzip;
            std::cout << "gzip: " << text.size() << " bytes to " << gzip.compress(text).size() << ", and back to "
                      << gzip.decompress(gzip.compress(text)).size() << "\n\n";
        }
        const auto zip = sc::archive::open(file);
        if (const auto folder = args.as<std::string>("extract"); !folder.empty()) {
            zip.extract(folder, args.as<std::string>("prefix"));
            std::cout << "Extracted into " << folder << '\n';
        } else {
            list(zip);
        }
        // [/readme]
    } catch (const std::exception &error) {
        std::cerr << "sc-archive-demo: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
