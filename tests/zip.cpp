// ZIP archives: writing (from memory and files, stored and deflated), reading back from a file
// and from memory, names, replacing, extracting (with a prefix, and refusing paths outside), errors.
#include <compression.h>
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
    using namespace sc::compression;
    using std::string;
    const auto root = fs::temp_directory_path() / ("sc-compression-test-" + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    const auto archive = root / "test.zip";
    const string big(50000, 'z');
    { std::ofstream{root / "photo.jpg", std::ios::binary} << string("\xff\xd8\xff\xe0 not really a jpeg", 21); }

    SECTION("Writing: from memory and from a file, stored or deflated, a name added again replaced");
    {
        zip_writer out{archive};
        out.add("hello.txt", "Hello");
        out.add("word/media/big.txt", big);
        out.add("word/media/raw.bin", string("\0\1\2", 3), zip_method::store);
        out.add_file("photos/photo.jpg", root / "photo.jpg", zip_method::store);
        out.add("hello.txt", "Hello again"); // replaces
        CHECK_THROWS_AS(out.add("", "x"), std::invalid_argument);
        CHECK_THROWS_AS(out.add("x", "x", zip_method::zstd), std::invalid_argument);
        CHECK_THROWS_AS(out.add_file("x", root / "missing"), std::runtime_error);
        out.close();
        CHECK(fs::exists(archive));
    }

    SECTION("Reading back: entries in order, sizes, methods, bytes");
    {
        const zip_reader zip{archive};
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
                CHECK(entry.method == zip_method::deflate);
                CHECK_EQ(entry.size, std::uint64_t{50000});
                CHECK(entry.compressed_size < 1000);
            }
            if (entry.name == "photos/photo.jpg") CHECK(entry.method == zip_method::store);
        }
        CHECK_THROWS_AS((void) zip.read("nothing"), std::out_of_range);
    }

    SECTION("From memory: the same archive's bytes");
    {
        const auto zip = zip_reader::from_memory(slurp(archive));
        CHECK_EQ(zip.read("word/media/big.txt"), big);
        CHECK_EQ(zip.entries().size(), std::size_t{4});
        CHECK_THROWS_AS((void) zip_reader::from_memory("not a zip"), std::runtime_error);
    }

    SECTION("Extracting: all, or by prefix, paths kept");
    {
        const zip_reader zip{archive};
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
            zip_writer out{evil};
            out.add("fine.txt", "ok");
            out.add("../escaped.txt", "bad");
            out.close();
        }
        const zip_reader zip{evil};
        CHECK_THROWS_AS(zip.extract(root / "target"), std::runtime_error);
        CHECK(!fs::exists(root / "escaped.txt"));
        CHECK(!fs::exists(root / "target" / "fine.txt")); // checked before anything is written
    }

    SECTION("Not an archive: runtime_error");
    {
        { std::ofstream{root / "plain.txt"} << "just text"; }
        CHECK_THROWS_AS(zip_reader{root / "plain.txt"}, std::runtime_error);
        CHECK_THROWS_AS(zip_reader{root / "missing.zip"}, std::runtime_error);
    }

    fs::remove_all(root);
    TEST_SUMMARY();
}
