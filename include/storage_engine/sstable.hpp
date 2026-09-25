#pragma once

#include "types.hpp"

class SSTable {
    std::string path_;

public:
    SSTable(std::string path);

    // returns `false` on error
    [[nodiscard]]
    bool get(const ByteVec& key, ByteVec& value) const noexcept;
};
