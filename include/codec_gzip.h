#pragma once

#include <codec.h>

namespace sc::compression {
    // gzip (RFC 1952, zlib), as in .gz files. decompress() also reads several members one after the
    // other, as `cat a.gz b.gz` makes.
    class gzip : public codec {
    public:
        // level: 0 (none) to 9 (smallest); throws std::invalid_argument for anything else.
        explicit gzip(int level = 6);

        [[nodiscard]] std::string name() const override { return "gzip"; }
        [[nodiscard]] std::string compress(std::string_view data) const override;
        [[nodiscard]] std::string decompress(std::string_view data) const override;

    private:
        int level_;
    };
}
