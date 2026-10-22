# simply-cpp-archive

`sc-archive` reads and writes archives (many files in one stream) and compresses
single streams. They are two separate things, so there are two class families:

- **Formats** (`sc::archive::format`): the structure inside an archive. ZIP
  (libzip), and with it `.docx`, `.xlsx`, `.pptx`, `.jar`, `.epub`; tar (our
  own code: ustar and pax, reading GNU long names too).
- **Codecs** (`sc::compression::codec`): the compression of one stream of
  bytes. gzip and raw deflate (zlib) for now.

`sc::archive` is the front: `open()` finds the format from a file's first bytes,
decompressing first when a codec wraps it (a `.tar.gz`), and `create()` writes one.

**Documentation:** the [simply-cpp wiki](https://github.com/roelofrossouw/simply-cpp/wiki) has an [Archive guide](https://github.com/roelofrossouw/simply-cpp/wiki/Archive) and the [sc-archive reference](https://github.com/roelofrossouw/simply-cpp/wiki/Reference-sc-archive).

```cpp
#include <sc_archive.h>
#include <codec_gzip.h>

const auto docx = sc::archive::open("letter.docx");
for (const auto &entry: docx.entries()) entry.name, entry.size, entry.method;
const std::string picture = docx.read("word/media/image1.png"); // std::out_of_range if absent
docx.extract("out", "word/media/");                            // refuses paths outside out/

auto out = sc::archive::create("archive.zip");
out.add("notes/readme.txt", "Hello");                         // deflated
out.add_file("photos/a.jpg", "/data/a.jpg", sc::archive::method::store);
out.close();                                                  // nothing is written before

auto tgz = sc::archive::create("backup.tar.gz", sc::archive::kind::tar,
                               std::make_shared<sc::compression::gzip>());
tgz.add("notes/", "");                                        // a folder
tgz.add_file("notes/readme.txt", "/data/readme.txt");
tgz.close();

const std::string packed = sc::compression::gzip{}.compress(text);  // .gz
const std::string raw = sc::compression::deflate{9}.compress(data); // RFC 1951, as in ZIP and PDF
if (const auto codec = sc::compression::detect(bytes)) text = codec->decompress(bytes);
```

| Header | Class | |
|---|---|---|
| `sc_archive.h` | `sc::archive::reader`, `writer`, `open()`, `from_memory()`, `create()` | the front |
| `archive_format.h` | `sc::archive::format::reader`, `writer`; `entry`, `method` | what each format implements |
| `archive_zip.h` | `sc::archive::format::zip_reader`, `zip_writer` | ZIP (libzip) |
| `archive_tar.h` | `sc::archive::format::tar_reader`, `tar_writer` | tar (our own) |
| `codec.h` | `sc::compression::codec`, `detect()` | what each codec implements |
| `codec_gzip.h` | `sc::compression::gzip` | gzip (zlib) |
| `codec_deflate.h` | `sc::compression::deflate` | raw deflate (zlib) |

- A reader opens a file or `from_memory(bytes)`. Entries come in the archive's
  order, with their size, compressed size, method (`store`, `deflate`, `bzip2`,
  `xz`, `zstd`, `other`), time and whether they're a folder. ZIP reading handles
  whatever methods the system's libzip was built with.
- Writers take entries from memory or from files (read at `close()`); a name
  ending in `/` is a folder, a name added again replaces the earlier entry, and
  an existing file is replaced. ZIP deflates entries by default (or stores
  them); tar stores them, and `create()`'s codec compresses the whole archive
  (ZIP takes none).
- tar: files and folders are listed, links and devices skipped. A leading `./`
  is dropped from names, and a name that comes again (tar appends updates)
  replaces the earlier entry. Long names and sizes past 8 GiB get pax headers.
  A tar archive is read into memory whole.
- `extract()` writes entries into a folder, keeping their paths, and refuses an
  entry that would land outside it (`../x`, an absolute path), before writing
  anything. It's shared by every format.
- A codec's level (0 to 9, 6 by default) goes in its constructor. gzip's
  `decompress()` reads several members one after the other, as `cat a.gz b.gz`
  makes. Bad or cut-short data throws `std::runtime_error`.
- Readers and writers aren't thread-safe: one per thread, or a lock around it.

## Libraries and licences

| Library | For | Licence |
|---|---|---|
| zlib | the deflate and gzip codecs | zlib License |
| libzip | the ZIP format | BSD-3-Clause |

Both are linked from the system (`zlib1g-dev`, `libzip-dev`; Homebrew `zlib`,
`libzip`), so their notices come with their own packages. sc-archive itself is
Apache-2.0.

**TODO:** bzip2, xz and zstd codecs (libbz2, liblzma, libzstd: all permissive,
packaged on every supported Ubuntu and in Homebrew), and with them `.tar.bz2`,
`.tar.xz`, `.tar.zst`.

## Demo

`sc-archive-demo [FILE] [--extract FOLDER] [--prefix PATH]` lists an archive's
(ZIP or tar, gzipped or not) entries, or extracts them; without a file it makes a small archive and a gzip
stream in the temporary folder and shows them.

<!-- sc-example: examples/sc-archive-demo.cpp -->
```cpp
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
```
<!-- /sc-example -->

## Building and testing

Run `bash scripts/install.sh` to configure, build, test, package and install.
The tests (`codecs`, `zip`, `tar`, label `unit`) need no network or services.
