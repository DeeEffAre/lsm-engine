#pragma once

#include "types.hpp"

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

    void put(ByteVec key, ByteVec value) noexcept;

    [[nodiscard]]
    const ByteVec* get(ByteSpan key) const noexcept;

    ByteVec* get(ByteSpan key) noexcept;

    bool write_to(std::ostream& stream) const noexcept;

    void clear() noexcept;
};
