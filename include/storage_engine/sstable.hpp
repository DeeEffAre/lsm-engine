#pragma once

#include "types.hpp"

#include <expected>

enum class SSTableError : uint8_t {
    FILE_ERROR,
    NOT_FOUND,
};

class SSTable {
    std::string path_;

public:
    SSTable(std::string path);

    [[nodiscard]]
    std::expected<ByteVec, SSTableError> get(ByteSpan key) const noexcept;
};
