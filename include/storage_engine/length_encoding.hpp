#pragma once

#include "storage_engine/errors.hpp"
#include "storage_engine/types.hpp"

#include <expected>

struct EncodedVarint {
    std::array<std::byte, 5> data{};
    size_t size{};
};

struct DecodedVarint {
    uint32_t value{};
    size_t bytes_read{};
};

class ByteReader {
    ByteSpan bytes_;
    size_t pos_{};

    [[nodiscard]]
    size_t remaining() const noexcept {
        return bytes_.size() - pos_;
    }

    [[nodiscard]]
    bool at_end() const noexcept {
        return pos_ >= bytes_.size();
    }

public:
    ByteReader(ByteSpan input) : bytes_(input) {}
    // reads leb128/varint encoded length prefix
    std::expected<uint32_t, ErrorCode> read_varint();
    // reads the binary encoded value
    std::expected<ByteSpan, ErrorCode> read_bytes(size_t payload_length);
};

class StreamReader {
    std::istream& file_; // NOLINT

public:
    StreamReader(std::istream& file_stream) : file_(file_stream) {}
    // reads leb128/varint encoded length prefix
    std::expected<uint32_t, ErrorCode> read_varint();
    // reads the binary encoded value
    std::expected<ByteVec, ErrorCode> read_bytes(size_t payload_length);
    std::expected<void, ErrorCode> skip(size_t length);
};

constexpr EncodedVarint encode_leb128(uint32_t value) noexcept {
    EncodedVarint output{};

    while (output.size < 5) {
        const uint8_t byte = value & 0x7F; // lowest 7 bit;
        value >>= 7;                       // shift 7 for continuation bit

        if (value == 0) {
            output.data[output.size++] = std::byte{byte}; // NOLINT
            break;
        }

        output.data[output.size++] =                      // NOLINT
            std::byte{static_cast<uint8_t>(byte | 0x80)}; // set continuation bit
    }

    return output;
}

constexpr std::expected<DecodedVarint, ErrorCode> decode_leb128(ByteSpan input) noexcept {
    uint32_t result = 0;
    int shift = 0;
    size_t bytes_read = 0;

    for (auto b : input) {
        auto byte = static_cast<uint8_t>(b);
        bytes_read++;

        // is at 5th byte
        if (shift == 28) {
            // are _xxx ____ bits set
            if ((byte & 0x70) != 0) {
                return std::unexpected(ErrorCode::OVERFLOW);
            }

            // is continuation bit set
            if ((byte & 0x80) != 0) {
                return std::unexpected(ErrorCode::TOO_MANY_BYTES);
            }
        }

        result |= static_cast<uint32_t>(byte & 0x7F) << shift;

        // is continuation bit set
        if ((byte & 0x80) == 0) {
            return DecodedVarint{.value = result, .bytes_read = bytes_read};
        }

        shift += 7;
    }

    return std::unexpected(ErrorCode::UNEXPECTED_END);
}
