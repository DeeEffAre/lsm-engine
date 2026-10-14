#pragma once

#include "storage_engine/errors.hpp"
#include "types.hpp"

#include <expected>

class SSTable {
    std::string path_;

public:
    SSTable(std::string path) : path_(std::move(path)) {}
    SSTable(std::string_view path) : path_(path) {}
    SSTable(const char* path) : path_(path) {}

    [[nodiscard]]
    std::expected<ByteVec, ErrorCode> get(ByteSpan key) const noexcept;
};
