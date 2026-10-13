#pragma once

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

using ByteVec = std::vector<std::byte>;
using ByteSpan = std::span<const std::byte>;

// A byte buffer that holds a length-prefixed record: {varint length}{payload}.
// It is a distinct type on purpose: raw bytes must not be passed where an
// encoded record is expected.
class EncodedByteVec {
public:
    EncodedByteVec() = default;
    explicit EncodedByteVec(ByteVec bytes) : bytes_(std::move(bytes)) {}

    [[nodiscard]] std::byte* data() noexcept { return bytes_.data(); }
    [[nodiscard]] const std::byte* data() const noexcept { return bytes_.data(); }
    [[nodiscard]] size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }
    void resize(size_t n) { bytes_.resize(n); }

    // Encoded bytes can be observed as raw bytes, but not the other way around.
    operator ByteSpan() const noexcept { return bytes_; }

private:
    ByteVec bytes_;
};
