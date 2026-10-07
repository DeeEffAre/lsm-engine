#pragma once

#include "storage_engine/errors.hpp"
#include "types.hpp"

#include <expected>

class SSTable {
    std::string path_;

public:
    SSTable(std::string path);

    [[nodiscard]]
    std::expected<ByteVec, ErrorCode> get(ByteSpan key) const noexcept;
};
