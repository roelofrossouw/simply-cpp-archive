#include "codec.h"

#include "codec_gzip.h"

namespace sc::compression {
    std::unique_ptr<codec> detect(const std::string_view bytes) {
        if (bytes.starts_with("\x1f\x8b")) return std::make_unique<gzip>();
        return nullptr;
    }
}
