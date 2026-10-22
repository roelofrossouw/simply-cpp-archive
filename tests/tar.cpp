// tar archives through sc::archive: writing (from memory and files, folders, long names), reading
// back from a file, from memory and as .tar.gz, GNU long names and "./" names written by hand,
// extracting, and errors.
#include <archive_tar.h>
#include <codec_gzip.h>
#include <sc_archive.h>
#include <sc_test.h>

#include <cstdio>
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

    // A ustar header as a tar program writes it, for the cases our writer doesn't make.
    std::string header(const std::string &name, const std::size_t size, const char type) {
        std::string out(512, '\0');
        out.replace(0, name.size(), name);
        char number[16];
        std::snprintf(number, sizeof number, "%07o", 0644);
        out.replace(100, 8, number, 8);
        std::snprintf(number, sizeof number, "%011zo", size);
        out.replace(124, 12, number, 12);
        std::snprintf(number, sizeof number, "%011o", 1700000000);
        out.replace(136, 12, number, 12);
        out[156] = type;
        out.replace(257, 8, "ustar  \0", 8); // GNU's magic
        out.replace(148, 8, 8, ' ');
        unsigned sum = 0;
        for (const char c: out) sum += static_cast<unsigned char>(c);
        std::snprintf(number, sizeof number, "%06o", sum);
        out.replace(148, 7, number, 7);
        return out;
    }

    std::string padded(std::string data) {
        data.append((512 - data.size() % 512) % 512, '\0');
        return data;
    }
}

int main() {
    using namespace sc::archive;
    using std::string;
    const auto root = fs::temp_directory_path() / ("sc-archive-tar-test-" + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    const string big(70000, 'z');
    const string long_name = string(120, 'n') + ".txt";                          // no '/' to split at: pax
    const string deep = string(80, 'd') + "/" + string(60, 'e') + "/file.txt"; // split into ustar's prefix
    { std::ofstream{root / "photo.jpg", std::ios::binary} << string("\xff\xd8\xff\xe0 not really a jpeg", 21); }

    SECTION("Writing: from memory and a file, a folder, long names, a name added again replaced");
    {
        auto out = create(root / "test.tar", kind::tar);
        out.add("hello.txt", "Hello");
        out.add("notes/", "");
        out.add("notes/big.txt", big);
        out.add_file("photos/photo.jpg", root / "photo.jpg");
        out.add(long_name, "long");
        out.add(deep, "deep");
        out.add("raw.bin", string("\0\1\2", 3), method::store);
        out.add("hello.txt", "Hello again");
        CHECK_THROWS_AS(out.add("", "x"), std::invalid_argument);
        CHECK_THROWS_AS(out.add("x", "x", method::deflate), std::invalid_argument);
        CHECK_THROWS_AS(out.add_file("x", root / "missing"), std::runtime_error);
        out.close();
        CHECK_EQ(fs::file_size(root / "test.tar") % 512, std::uintmax_t{0});
        CHECK_THROWS_AS(create(root / "x.zip", kind::zip, std::make_shared<sc::compression::gzip>()), std::invalid_argument);
    }

    SECTION("Reading back: entries in order, folders, sizes, bytes, long names");
    {
        const auto tar = open(root / "test.tar");
        CHECK_EQ(tar.entries().size(), std::size_t{7});
        CHECK_EQ(tar.entries()[0].name, string{"hello.txt"});
        CHECK_EQ(tar.entries()[1].name, string{"notes/"});
        CHECK(tar.entries()[1].folder);
        CHECK_EQ(tar.read("hello.txt"), string{"Hello again"});
        CHECK_EQ(tar.read("notes/big.txt"), big);
        CHECK_EQ(tar.read("photos/photo.jpg"), slurp(root / "photo.jpg"));
        CHECK_EQ(tar.read(long_name), string{"long"});
        CHECK_EQ(tar.read(deep), string{"deep"});
        CHECK_EQ(tar.read("raw.bin"), string("\0\1\2", 3));
        CHECK(tar.entries()[2].method == method::store);
        CHECK_EQ(tar.entries()[2].size, std::uint64_t{70000});
        CHECK(tar.entries()[0].modified > std::chrono::system_clock::now() - std::chrono::hours{1});
        CHECK_THROWS_AS((void) tar.read("nothing"), std::out_of_range);
    }

    SECTION(".tar.gz: written with the gzip codec, read from a file and from memory");
    {
        auto out = create(root / "test.tar.gz", kind::tar, std::make_shared<sc::compression::gzip>());
        out.add("notes/big.txt", big);
        out.close();
        const auto packed = slurp(root / "test.tar.gz");
        CHECK(packed.size() < 2000);
        CHECK_EQ(open(root / "test.tar.gz").read("notes/big.txt"), big);
        CHECK_EQ(from_memory(packed).entries().size(), std::size_t{1});
        CHECK_EQ(from_memory(slurp(root / "test.tar")).read("hello.txt"), string{"Hello again"});
    }

    SECTION("As tar programs write it: GNU long names, \"./\" names, links skipped, a name again replaced");
    {
        const string name = "./" + string(150, 'g') + ".txt";
        string tar = header("././@LongLink", name.size() + 1, 'L') + padded(name + '\0');
        tar += header("ignored-short-name", 3, '0') + padded("abc");
        tar += header("./", 0, '5');
        tar += header("./dir", 0, '5');
        tar += header("./dir/link", 0, '2');
        tar += header("./dir/a.txt", 2, '0') + padded("v1");
        tar += header("./dir/a.txt", 2, '0') + padded("v2");
        tar += string(1024, '\0');
        const auto read = from_memory(tar);
        CHECK_EQ(read.entries().size(), std::size_t{3});
        CHECK_EQ(read.read(name.substr(2)), string{"abc"});
        CHECK(read.contains("dir/"));
        CHECK(!read.contains("dir/link"));
        CHECK_EQ(read.read("dir/a.txt"), string{"v2"});
        CHECK_EQ(read.entries()[2].name, string{"dir/a.txt"});
    }

    SECTION("Extracting, and refusing an entry outside the folder");
    {
        open(root / "test.tar").extract(root / "all");
        CHECK_EQ(slurp(root / "all" / "notes" / "big.txt"), big);
        CHECK(fs::is_directory(root / "all" / "notes"));
        {
            auto out = create(root / "evil.tar", kind::tar);
            out.add("fine.txt", "ok");
            out.add("../escaped.txt", "bad");
            out.close();
        }
        CHECK_THROWS_AS(open(root / "evil.tar").extract(root / "target"), std::runtime_error);
        CHECK(!fs::exists(root / "escaped.txt"));
        CHECK(!fs::exists(root / "target" / "fine.txt"));
    }

    SECTION("Not tar, or broken: runtime_error");
    {
        CHECK(!format::tar_reader::recognizes(string(512, '\0')));
        CHECK(!format::tar_reader::recognizes("short"));
        auto tar = slurp(root / "test.tar");
        CHECK_THROWS_AS((void) from_memory(tar.substr(0, 5000)), std::runtime_error); // cut short, in big.txt
        tar[1024 + 10] ^= 1;                                                            // the second header
        CHECK_THROWS_AS((void) from_memory(tar), std::runtime_error);
        CHECK_THROWS_AS((void) from_memory(sc::compression::gzip{}.compress("not tar")), std::runtime_error);
    }

    fs::remove_all(root);
    TEST_SUMMARY();
}
