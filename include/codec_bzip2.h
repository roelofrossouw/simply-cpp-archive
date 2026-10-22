#pragma once

#include <codec.h>

namespace sc::compression {
    // bzip2 (libbz2), as in .bz2 files. decompress() also reads several streams one after the
    // other, as `cat a.bz2 b.bz2` makes (and pbzip2 writes).
    class bzip2 : public codec {
    public:
        // level: 1 (fastest, 100k blocks) to 9 (smallest, 900k blocks), 9 by default.
        // Throws std::invalid_argument for anything else.
        explicit bzip2(int level = 9);

        [[nodiscard]] std::string name() const override { return "bzip2"; }
        [[nodiscard]] std::string compress(std::string_view data) const override;
        [[nodiscard]] std::string decompress(std::string_view data) const override;

    private:
        int level_;
    };
}
