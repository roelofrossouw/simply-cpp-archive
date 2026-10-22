# simply-cpp-compression

`sc-compression` reads and writes ZIP archives (libzip) and compresses single
streams with deflate and gzip (zlib). Formats built on ZIP read the same way:
`.docx`, `.xlsx`, `.pptx`, `.jar`, `.epub`.

**Documentation:** the [simply-cpp wiki](https://github.com/roelofrossouw/simply-cpp/wiki) has a [Compression guide](https://github.com/roelofrossouw/simply-cpp/wiki/Compression) and the [sc-compression reference](https://github.com/roelofrossouw/simply-cpp/wiki/Reference-sc-compression).

```cpp
#include <compression.h>

sc::compression::zip_reader docx{"letter.docx"};
for (const auto &entry: docx.entries()) entry.name, entry.size, entry.method;
const std::string picture = docx.read("word/media/image1.png"); // std::out_of_range if absent
docx.extract("out", "word/media/");                            // refuses paths outside out/

sc::compression::zip_writer out{"archive.zip"};
out.add("notes/readme.txt", "Hello");                         // deflated
out.add_file("photos/a.jpg", "/data/a.jpg", sc::compression::zip_method::store);
out.close();                                                  // nothing is written before

const std::string packed = sc::compression::gzip(text);       // .gz
const std::string raw = sc::compression::deflate(data, 9);    // RFC 1951, as in ZIP and PDF
```

- `zip_reader` reads from a file or `zip_reader::from_memory(bytes)`. Entries
  come in the archive's order, with their size, compressed size, method
  (`store`, `deflate`, `bzip2`, `xz`, `zstd`, `other`), time and whether
  they're a folder. Reading handles whatever methods the system's libzip was
  built with.
- `zip_writer` writes stored or deflated entries, from memory or from files
  (read at `close()`); an existing file is replaced. A name added again
  replaces the earlier entry.
- `extract()` writes entries into a folder, keeping their paths, and refuses an
  entry that would land outside it (`../x`, an absolute path), before writing
  anything.
- `gunzip()` reads several gzip members one after the other, as `cat a.gz b.gz`
  makes. Bad or cut-short data throws `std::runtime_error`.
- Neither class is thread-safe: one per thread, or a lock around it.

## Libraries and licences

| Library | For | Licence |
|---|---|---|
| zlib | deflate and gzip | zlib License |
| libzip | ZIP archives | BSD-3-Clause |

Both are linked from the system (`zlib1g-dev`, `libzip-dev`; Homebrew `zlib`,
`libzip`), so their notices come with their own packages. sc-compression itself
is Apache-2.0.

**TODO:** bzip2, xz and zstd streams (libbz2, liblzma, libzstd: all permissive,
packaged on every supported Ubuntu and in Homebrew).

## Demo

`sc-compression-demo [FILE] [--extract FOLDER] [--prefix PATH]` lists a ZIP
archive's entries, or extracts them; without a file it makes a small archive and
a gzip stream in the temporary folder and shows them.

<!-- sc-example: examples/sc-compression-demo.cpp -->
```cpp
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
```
<!-- /sc-example -->

## Building and testing

Run `bash scripts/install.sh` to configure, build, test, package and install.
The tests (`streams`, `zip`, label `unit`) need no network or services.
