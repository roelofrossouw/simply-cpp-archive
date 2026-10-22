#pragma once

#include <memory>
#include <string>
#include <string_view>

// The compression of one stream of bytes, whatever is in it: a codec compresses and decompresses.
// Each one is its own class (codec_gzip.h, codec_deflate.h).
//
//     const std::string packed = sc::compression::gzip{}.compress(text);
//     if (const auto codec = sc::compression::detect(bytes)) text = codec->decompress(bytes);
//
// TODO: bzip2, xz and zstd (libbz2, liblzma, libzstd: all permissive, packaged everywhere).
namespace sc::compression {
    class codec {
    public:
        virtual ~codec() = default;

        // "gzip", "deflate".
        [[nodiscard]] virtual std::string name() const = 0;
        [[nodiscard]] virtual std::string compress(std::string_view data) const = 0;
        // Throws std::runtime_error for data that isn't valid for this codec, or is cut short.
        [[nodiscard]] virtual std::string decompress(std::string_view data) const = 0;
    };

    // The codec whose header the bytes start with (gzip's 1f 8b); null when none does. Raw deflate
    // has no header, so it is never detected.
    std::unique_ptr<codec> detect(std::string_view bytes);
}
