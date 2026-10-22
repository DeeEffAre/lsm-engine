#pragma once

#include "storage_engine/errors.hpp"
#include "types.hpp"

#include <expected>

class Memtable {
    struct Impl; // Forward declaration
    std::unique_ptr<Impl> pimpl_;

public:
    Memtable();
    ~Memtable();
    Memtable(const Memtable&) = delete;
    Memtable& operator=(const Memtable&) = delete;
    Memtable(Memtable&&) noexcept;
    Memtable& operator=(Memtable&&) noexcept;

    // supports both PUT and DELETE operations
    void put(RecordType record_type, ByteVec key, ByteVec value) noexcept;

    [[nodiscard]]
    auto get(ByteSpan key) const noexcept -> std::expected<const ByteVec*, ErrorCode>;

    auto get(ByteSpan key) noexcept -> std::expected<ByteVec*, ErrorCode>;

    bool write_to(std::ostream& stream) const noexcept;

    void clear() noexcept;
};
