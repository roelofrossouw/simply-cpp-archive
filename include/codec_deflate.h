#pragma once

#include <codec.h>

namespace sc::compression {
    // Raw deflate (RFC 1951, zlib), as inside ZIP entries and PDF streams.
    class deflate : public codec {
    public:
        // level: 0 (none) to 9 (smallest); throws std::invalid_argument for anything else.
        explicit deflate(int level = 6);

        [[nodiscard]] std::string name() const override { return "deflate"; }
        [[nodiscard]] std::string compress(std::string_view data) const override;
        [[nodiscard]] std::string decompress(std::string_view data) const override;

    private:
        int level_;
    };
}
