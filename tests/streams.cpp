// deflate and gzip: round trips, levels, empty and large data, several gzip members, and errors.
#include <compression.h>
#include <sc_test.h>

#include <random>
#include <string>

int main() {
    using namespace sc::compression;
    using std::string;
    std::mt19937 random{42};
    string noise(200000, '\0');
    for (auto &c: noise) c = static_cast<char>(random());
    const string text = string(100000, 'a') + "the end";

    SECTION("deflate and inflate: round trips");
    {
        CHECK_EQ(inflate(deflate(text)), text);
        CHECK_EQ(inflate(deflate(noise)), noise);
        CHECK_EQ(inflate(deflate("")), string{});
        CHECK(deflate(text).size() < 1000);                 // repetitive text shrinks a lot
        CHECK(deflate(text, 0).size() > text.size());       // level 0 stores
        CHECK(deflate(text, 9).size() <= deflate(text, 1).size());
        CHECK_THROWS_AS((void) deflate(text, 10), std::invalid_argument);
    }

    SECTION("gzip and gunzip: round trips, a .gz header, several members");
    {
        const auto packed = gzip(text);
        CHECK_EQ(static_cast<unsigned char>(packed[0]), 0x1fu);
        CHECK_EQ(static_cast<unsigned char>(packed[1]), 0x8bu);
        CHECK_EQ(gunzip(packed), text);
        CHECK_EQ(gunzip(gzip("first ") + gzip("second")), string{"first second"}); // as cat a.gz b.gz
        CHECK_EQ(gunzip(gzip(noise)), noise);
    }

    SECTION("Not compressed data, or cut short: runtime_error");
    {
        CHECK_THROWS_AS((void) inflate("not deflate at all"), std::runtime_error);
        CHECK_THROWS_AS((void) gunzip("not gzip"), std::runtime_error);
        const auto packed = gzip(text);
        CHECK_THROWS_AS((void) gunzip(packed.substr(0, packed.size() / 2)), std::runtime_error);
        CHECK_THROWS_AS((void) inflate(deflate(text).substr(0, 10)), std::runtime_error);
    }

    TEST_SUMMARY();
}
