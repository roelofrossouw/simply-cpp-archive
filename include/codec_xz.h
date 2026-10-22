#pragma once

#include <codec.h>

namespace sc::compression {
    // xz (liblzma), as in .xz files. decompress() also reads several streams one after the other.
    class xz : public codec {
    public:
        // level: 0 (fastest) to 9 (smallest, and much slower), 6 by default.
        // Throws std::invalid_argument for anything else.
        explicit xz(int level = 6);

        [[nodiscard]] std::string name() const override { return "xz"; }
        [[nodiscard]] std::string compress(std::string_view data) const override;
        [[nodiscard]] std::string decompress(std::string_view data) const override;

    private:
        int level_;
    };
}
