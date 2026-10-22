#include "codec_zstd.h"

#include <zstd.h>

#include <memory>
#include <stdexcept>

namespace sc::compression {
    zstd::zstd(const int level) : level_(level) {
        if (level < 1 || level > 22) throw std::invalid_argument("zstd: the level is 1 to 22");
    }

    std::string zstd::compress(const std::string_view data) const {
        std::string out(ZSTD_compressBound(data.size()), '\0');
        const auto size = ZSTD_compress(out.data(), out.size(), data.data(), data.size(), level_);
        if (ZSTD_isError(size)) throw std::runtime_error(std::string{"zstd: compressing failed: "} + ZSTD_getErrorName(size));
        out.resize(size);
        return out;
    }

    std::string zstd::decompress(const std::string_view data) const {
        const std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context{ZSTD_createDCtx(), ZSTD_freeDCtx};
        if (!context) throw std::runtime_error("zstd: can't start decompressing");
        std::string out;
        char buffer[64 * 1024];
        ZSTD_inBuffer in{data.data(), data.size(), 0};
        std::size_t left = 1; // 0 once a frame is complete
        while (in.pos < in.size) { // frame after frame
            ZSTD_outBuffer chunk{buffer, sizeof buffer, 0};
            left = ZSTD_decompressStream(context.get(), &chunk, &in);
            if (ZSTD_isError(left)) throw std::runtime_error(std::string{"zstd: not valid zstd data: "} + ZSTD_getErrorName(left));
            out.append(buffer, chunk.pos);
        }
        while (left != 0) { // the last frame's remaining output, if any is held back
            ZSTD_outBuffer chunk{buffer, sizeof buffer, 0};
            left = ZSTD_decompressStream(context.get(), &chunk, &in);
            if (ZSTD_isError(left)) throw std::runtime_error(std::string{"zstd: not valid zstd data: "} + ZSTD_getErrorName(left));
            out.append(buffer, chunk.pos);
            if (chunk.pos == 0 && left != 0) throw std::runtime_error("zstd: the data is cut short");
        }
        return out;
    }
}
