#pragma once

#include <memory>
#include <string>
#include <string_view>

// The compression of one stream of bytes, whatever is in it: a codec compresses and decompresses.
// Each one is its own class (codec_gzip.h, codec_deflate.h, codec_bzip2.h, codec_xz.h, codec_zstd.h).
//
//     const std::string packed = sc::compression::gzip{}.compress(text);
//     if (const auto codec = sc::compression::detect(bytes)) text = codec->decompress(bytes);
namespace sc::compression {
    class codec {
    public:
        virtual ~codec() = default;

        // "gzip", "deflate", "bzip2", "xz", "zstd".
        [[nodiscard]] virtual std::string name() const = 0;
        [[nodiscard]] virtual std::string compress(std::string_view data) const = 0;
        // Throws std::runtime_error for data that isn't valid for this codec, or is cut short.
        [[nodiscard]] virtual std::string decompress(std::string_view data) const = 0;
    };

    // The codec whose header the bytes start with (gzip 1f 8b, bzip2 "BZh", xz fd "7zXZ", zstd
    // 28 b5 2f fd), at its default level; null when none does. Raw deflate has no header, so it is
    // never detected.
    std::unique_ptr<codec> detect(std::string_view bytes);
}
