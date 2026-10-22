#include "codec_gzip.h"

#include "zlib_streams.h"

namespace sc::compression {
    gzip::gzip(const int level) : level_(zlib::checked_level(level)) {}

    std::string gzip::compress(const std::string_view data) const { return zlib::squeeze(data, level_, 15 + 16); }
    std::string gzip::decompress(const std::string_view data) const { return zlib::expand(data, 15 + 16, true); }
}
