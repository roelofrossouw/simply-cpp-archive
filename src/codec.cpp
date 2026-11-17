#include <sc/archive/codec.h>

#include <sc/archive/codec_bzip2.h>
#include <sc/archive/codec_gzip.h>
#include <sc/archive/codec_xz.h>
#include <sc/archive/codec_zstd.h>

namespace sc::compression {
    std::unique_ptr<codec> detect(const std::string_view bytes) {
        if (bytes.starts_with("\x1f\x8b")) return std::make_unique<gzip>();
        if (bytes.size() >= 4 && bytes.starts_with("BZh") && bytes[3] >= '1' && bytes[3] <= '9') return std::make_unique<bzip2>();
        if (bytes.starts_with(std::string_view{"\xfd" "7zXZ\0", 6})) return std::make_unique<xz>();
        if (bytes.starts_with("\x28\xb5\x2f\xfd")) return std::make_unique<zstd>();
        return nullptr;
    }
}
