// The deflate and gzip codecs: round trips, levels, empty and large data, several gzip members,
// detection, and errors.
#include <codec_deflate.h>
#include <codec_gzip.h>
#include <sc_test.h>

#include <random>
#include <string>

int main() {
    using namespace sc::compression;
    const deflate deflated;
    const gzip gzipped;
    using std::string;
    std::mt19937 random{42};
    string noise(200000, '\0');
    for (auto &c: noise) c = static_cast<char>(random());
    const string text = string(100000, 'a') + "the end";

    SECTION("deflate: round trips, levels");
    {
        CHECK_EQ(deflated.decompress(deflated.compress(text)), text);
        CHECK_EQ(deflated.decompress(deflated.compress(noise)), noise);
        CHECK_EQ(deflated.decompress(deflated.compress("")), string{});
        CHECK(deflated.compress(text).size() < 1000); // repetitive text shrinks a lot
        CHECK(deflate{0}.compress(text).size() > text.size()); // level 0 stores
        CHECK(deflate{9}.compress(text).size() <= deflate{1}.compress(text).size());
        CHECK_THROWS_AS(deflate{10}, std::invalid_argument);
        CHECK_THROWS_AS(gzip{-1}, std::invalid_argument);
    }

    SECTION("gzip: round trips, a .gz header, several members");
    {
        const auto packed = gzipped.compress(text);
        CHECK_EQ(static_cast<unsigned char>(packed[0]), 0x1fu);
        CHECK_EQ(static_cast<unsigned char>(packed[1]), 0x8bu);
        CHECK_EQ(gzipped.decompress(packed), text);
        CHECK_EQ(gzipped.decompress(gzipped.compress("first ") + gzipped.compress("second")), string{"first second"}); // as cat a.gz b.gz
        CHECK_EQ(gzipped.decompress(gzipped.compress(noise)), noise);
    }

    SECTION("Detection: gzip by its header; raw deflate and plain text have none");
    {
        const auto codec = detect(gzipped.compress(text));
        CHECK(codec != nullptr);
        CHECK_EQ(codec->name(), string{"gzip"});
        CHECK_EQ(codec->decompress(gzipped.compress(text)), text);
        CHECK(detect(deflated.compress(text)) == nullptr);
        CHECK(detect("plain text") == nullptr);
        CHECK(detect("") == nullptr);
    }

    SECTION("Not compressed data, or cut short: runtime_error");
    {
        CHECK_THROWS_AS((void) deflated.decompress("not deflate at all"), std::runtime_error);
        CHECK_THROWS_AS((void) gzipped.decompress("not gzip"), std::runtime_error);
        const auto packed = gzipped.compress(text);
        CHECK_THROWS_AS((void) gzipped.decompress(packed.substr(0, packed.size() / 2)), std::runtime_error);
        CHECK_THROWS_AS((void) deflated.decompress(deflated.compress(text).substr(0, 10)), std::runtime_error);
    }

    TEST_SUMMARY();
}
