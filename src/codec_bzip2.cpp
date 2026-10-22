#include "codec_bzip2.h"

#include <bzlib.h>

#include <stdexcept>

namespace sc::compression {
    bzip2::bzip2(const int level) : level_(level) {
        if (level < 1 || level > 9) throw std::invalid_argument("bzip2: the level is 1 to 9");
    }

    std::string bzip2::compress(const std::string_view data) const {
        bz_stream stream{};
        if (BZ2_bzCompressInit(&stream, level_, 0, 0) != BZ_OK) throw std::runtime_error("bzip2: can't start compressing");
        std::string out;
        char buffer[64 * 1024];
        stream.next_in = const_cast<char *>(data.data());
        stream.avail_in = static_cast<unsigned>(data.size());
        int result;
        do {
            stream.next_out = buffer;
            stream.avail_out = sizeof buffer;
            result = BZ2_bzCompress(&stream, BZ_FINISH);
            out.append(buffer, sizeof buffer - stream.avail_out);
        } while (result == BZ_FINISH_OK);
        BZ2_bzCompressEnd(&stream);
        if (result != BZ_STREAM_END) throw std::runtime_error("bzip2: compressing failed");
        return out;
    }

    std::string bzip2::decompress(const std::string_view data) const {
        std::string out;
        char buffer[64 * 1024];
        auto rest = data;
        do { // one stream at a time
            bz_stream stream{};
            if (BZ2_bzDecompressInit(&stream, 0, 0) != BZ_OK) throw std::runtime_error("bzip2: can't start decompressing");
            stream.next_in = const_cast<char *>(rest.data());
            stream.avail_in = static_cast<unsigned>(rest.size());
            int result;
            do {
                stream.next_out = buffer;
                stream.avail_out = sizeof buffer;
                result = BZ2_bzDecompress(&stream);
                out.append(buffer, sizeof buffer - stream.avail_out);
            } while (result == BZ_OK && (stream.avail_in > 0 || stream.avail_out == 0));
            rest.remove_prefix(rest.size() - stream.avail_in);
            BZ2_bzDecompressEnd(&stream);
            if (result != BZ_STREAM_END) throw std::runtime_error("bzip2: not valid bzip2 data, or cut short");
        } while (!rest.empty());
        return out;
    }
}
