#pragma once

#include <string>
#include <string_view>

// zlib's deflate, wrapped or not, for codec_deflate and codec_gzip.
namespace sc::compression::zlib {
    // window_bits: -15 raw deflate, 15 + 16 gzip (zlib's convention).
    std::string squeeze(std::string_view data, int level, int window_bits);
    // members: gzip members one after the other are read on (as gunzip does).
    std::string expand(std::string_view data, int window_bits, bool members);
    // Throws std::invalid_argument for a level outside 0 to 9.
    int checked_level(int level);
}
