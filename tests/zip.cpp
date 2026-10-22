// ZIP archives through sc::archive: writing (from memory and files, stored and deflated), reading
// back from a file, from memory and from inside gzip, names, replacing, extracting (with a prefix,
// and refusing paths outside), errors.
#include <sc_archive.h>
#include <codec_gzip.h>
#include <sc_test.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {
    std::string slurp(const fs::path &file) {
        std::ifstream in{file, std::ios::binary};
        std::stringstream all;
        all << in.rdbuf();
        return all.str();
    }
}

int main() {
    using namespace sc::archive;
    using std::string;
    const auto root = fs::temp_directory_path() / ("sc-archive-test-" + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    const auto archive = root / "test.zip";
    const string big(50000, 'z');
    { std::ofstream{root / "photo.jpg", std::ios::binary} << string("\xff\xd8\xff\xe0 not really a jpeg", 21); }

    SECTION("Writing: from memory and from a file, stored or deflated, a name added again replaced");
    {
        auto out = create(archive);
        out.add("hello.txt", "Hello");
        out.add("word/media/big.txt", big);
        out.add("word/media/raw.bin", string("\0\1\2", 3), method::store);
        out.add_file("photos/photo.jpg", root / "photo.jpg", method::store);
        out.add("hello.txt", "Hello again"); // replaces
        CHECK_THROWS_AS(out.add("", "x"), std::invalid_argument);
        CHECK_THROWS_AS(out.add("x", "x", method::zstd), std::invalid_argument);
        CHECK_THROWS_AS(out.add_file("x", root / "missing"), std::runtime_error);
        out.close();
        CHECK(fs::exists(archive));
    }

    SECTION("Reading back: entries in order, sizes, methods, bytes");
    {
        const auto zip = open(archive);
        CHECK_EQ(zip.entries().size(), std::size_t{4});
        CHECK_EQ(zip.entries()[0].name, string{"hello.txt"});
        CHECK(zip.contains("word/media/big.txt"));
        CHECK(!zip.contains("nothing"));
        CHECK_EQ(zip.read("hello.txt"), string{"Hello again"});
        CHECK_EQ(zip.read("word/media/big.txt"), big);
        CHECK_EQ(zip.read("word/media/raw.bin"), string("\0\1\2", 3));
        CHECK_EQ(zip.read("photos/photo.jpg"), slurp(root / "photo.jpg"));
        for (const auto &entry: zip.entries()) {
            if (entry.name == "word/media/big.txt") {
                CHECK(entry.method == method::deflate);
                CHECK_EQ(entry.size, std::uint64_t{50000});
                CHECK(entry.compressed_size < 1000);
            }
            if (entry.name == "photos/photo.jpg") CHECK(entry.method == method::store);
        }
        CHECK_THROWS_AS((void) zip.read("nothing"), std::out_of_range);
    }

    SECTION("From memory: the same archive's bytes, and inside gzip (as a file too)");
    {
        const auto zip = from_memory(slurp(archive));
        CHECK_EQ(zip.read("word/media/big.txt"), big);
        CHECK_EQ(zip.entries().size(), std::size_t{4});
        const auto packed = sc::compression::gzip{}.compress(slurp(archive));
        CHECK_EQ(from_memory(packed).read("hello.txt"), string{"Hello again"});
        { std::ofstream{root / "test.zip.gz", std::ios::binary} << packed; }
        CHECK_EQ(open(root / "test.zip.gz").read("hello.txt"), string{"Hello again"});
        CHECK_THROWS_AS((void) from_memory("not a zip"), std::runtime_error);
        CHECK_THROWS_AS((void) from_memory(sc::compression::gzip{}.compress("not a zip")), std::runtime_error);
    }

    SECTION("Extracting: all, or by prefix, paths kept");
    {
        const auto zip = open(archive);
        zip.extract(root / "all");
        CHECK_EQ(slurp(root / "all" / "word" / "media" / "big.txt"), big);
        CHECK_EQ(slurp(root / "all" / "hello.txt"), string{"Hello again"});
        zip.extract(root / "media", "word/media/");
        CHECK(fs::exists(root / "media" / "word" / "media" / "raw.bin"));
        CHECK(!fs::exists(root / "media" / "hello.txt"));
    }

    SECTION("An entry that would land outside the folder: refused, nothing written");
    {
        const auto evil = root / "evil.zip";
        {
            auto out = create(evil);
            out.add("fine.txt", "ok");
            out.add("../escaped.txt", "bad");
            out.close();
        }
        const auto zip = open(evil);
        CHECK_THROWS_AS(zip.extract(root / "target"), std::runtime_error);
        CHECK(!fs::exists(root / "escaped.txt"));
        CHECK(!fs::exists(root / "target" / "fine.txt")); // checked before anything is written
    }

    SECTION("Not an archive: runtime_error");
    {
        { std::ofstream{root / "plain.txt"} << "just text"; }
        CHECK_THROWS_AS((void) open(root / "plain.txt"), std::runtime_error);
        CHECK_THROWS_AS((void) open(root / "missing.zip"), std::runtime_error);
        { std::ofstream{root / "broken.zip", std::ios::binary} << "PK\3\4 and then nothing"; }
        CHECK_THROWS_AS((void) open(root / "broken.zip"), std::runtime_error);
    }

    fs::remove_all(root);
    TEST_SUMMARY();
}
