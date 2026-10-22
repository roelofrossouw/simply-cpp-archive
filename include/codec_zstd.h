#pragma once

#include <codec.h>

namespace sc::compression {
    // Zstandard (libzstd), as in .zst files. decompress() also reads several frames one after the
    // other.
    class zstd : public codec {
    public:
        // level: 1 (fastest) to 22 (smallest; past 19 it needs much more memory), 3 by default.
        // Throws std::invalid_argument for anything else.
        explicit zstd(int level = 3);

        [[nodiscard]] std::string name() const override { return "zstd"; }
        [[nodiscard]] std::string compress(std::string_view data) const override;
        [[nodiscard]] std::string decompress(std::string_view data) const override;

    private:
        int level_;
    };
}
