#include "codec_deflate.h"

#include "zlib_streams.h"

namespace sc::compression {
    deflate::deflate(const int level) : level_(zlib::checked_level(level)) {}

    std::string deflate::compress(const std::string_view data) const { return zlib::squeeze(data, level_, -15); }
    std::string deflate::decompress(const std::string_view data) const { return zlib::expand(data, -15, false); }
}
