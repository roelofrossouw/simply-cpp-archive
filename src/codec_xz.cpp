#include "codec_xz.h"

#include <lzma.h>

#include <cstdint>
#include <stdexcept>

namespace sc::compression {
    namespace {
        std::string run(lzma_stream &stream, const std::string_view data, const char *doing) {
            std::string out;
            std::uint8_t buffer[64 * 1024];
            stream.next_in = reinterpret_cast<const std::uint8_t *>(data.data());
            stream.avail_in = data.size();
            lzma_ret result;
            do {
                stream.next_out = buffer;
                stream.avail_out = sizeof buffer;
                result = lzma_code(&stream, LZMA_FINISH);
                out.append(reinterpret_cast<const char *>(buffer), sizeof buffer - stream.avail_out);
            } while (result == LZMA_OK);
            lzma_end(&stream);
            if (result != LZMA_STREAM_END) {
                throw std::runtime_error(std::string{"xz: "} + doing + " failed" +
                                         (result == LZMA_BUF_ERROR ? ": cut short" : result == LZMA_FORMAT_ERROR ? ": not xz data" : ""));
            }
            return out;
        }
    }

    xz::xz(const int level) : level_(level) {
        if (level < 0 || level > 9) throw std::invalid_argument("xz: the level is 0 to 9");
    }

    std::string xz::compress(const std::string_view data) const {
        lzma_stream stream = LZMA_STREAM_INIT;
        if (lzma_easy_encoder(&stream, static_cast<std::uint32_t>(level_), LZMA_CHECK_CRC64) != LZMA_OK) {
            throw std::runtime_error("xz: can't start compressing");
        }
        return run(stream, data, "compressing");
    }

    std::string xz::decompress(const std::string_view data) const {
        lzma_stream stream = LZMA_STREAM_INIT;
        if (lzma_stream_decoder(&stream, UINT64_MAX, LZMA_CONCATENATED) != LZMA_OK) {
            throw std::runtime_error("xz: can't start decompressing");
        }
        return run(stream, data, "decompressing");
    }
}
