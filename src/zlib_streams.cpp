#include "zlib_streams.h"

#include <zlib.h>

#include <stdexcept>

namespace sc::compression::zlib {
    int checked_level(const int level) {
        if (level < 0 || level > 9) throw std::invalid_argument("compression level is 0 to 9");
        return level;
    }

    std::string squeeze(const std::string_view data, const int level, const int window_bits) {
        z_stream stream{};
        if (deflateInit2(&stream, level, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
            throw std::runtime_error("zlib: can't start compressing");
        }
        std::string out(deflateBound(&stream, static_cast<uLong>(data.size())), '\0');
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
        stream.avail_in = static_cast<uInt>(data.size());
        stream.next_out = reinterpret_cast<Bytef *>(out.data());
        stream.avail_out = static_cast<uInt>(out.size());
        const int result = ::deflate(&stream, Z_FINISH);
        out.resize(stream.total_out);
        deflateEnd(&stream);
        if (result != Z_STREAM_END) throw std::runtime_error("zlib: compressing failed");
        return out;
    }

    std::string expand(const std::string_view data, const int window_bits, const bool members) {
        z_stream stream{};
        if (inflateInit2(&stream, window_bits) != Z_OK) throw std::runtime_error("zlib: can't start decompressing");
        std::string out;
        char buffer[64 * 1024];
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
        stream.avail_in = static_cast<uInt>(data.size());
        int result = Z_OK;
        while (true) {
            stream.next_out = reinterpret_cast<Bytef *>(buffer);
            stream.avail_out = sizeof buffer;
            result = ::inflate(&stream, Z_NO_FLUSH);
            out.append(buffer, sizeof buffer - stream.avail_out);
            if (result == Z_STREAM_END) {
                if (members && stream.avail_in > 0) {
                    inflateReset(&stream);
                    continue;
                }
                break;
            }
            if (result != Z_OK || (stream.avail_in == 0 && stream.avail_out != 0)) break; // an error, or data cut short
        }
        const char *message = stream.msg;
        inflateEnd(&stream);
        if (result != Z_STREAM_END) {
            throw std::runtime_error(std::string{"zlib: not valid compressed data"} + (message ? std::string{" ("} + message + ")" : ""));
        }
        return out;
    }
}
