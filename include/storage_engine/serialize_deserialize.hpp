#pragma once

#include "storage_engine/types.hpp"

#include <cstdint>
#include <cstring>
#include <expected>
#include <span>

// Formula:
// $length (4 byte) + $value ($length byte)

// TODO: generic contiguous container support (e.g. std::vector<T>)
template <typename T>
concept Serializable =
    std::is_trivially_copyable_v<T> || std::is_same_v<T, std::string> || std::is_same_v<T, ByteVec>;

struct EncodedVarint {
    std::array<std::byte, 5> data{};
    size_t size{};
};

constexpr EncodedVarint encode_leb128(uint32_t value) noexcept {
    EncodedVarint output{};

    while (output.size < 5) {
        uint8_t byte = value & 0x7F; // lowest 7 bit;
        value >>= 7;                 // shift 7 for continuation bit

        if (value == 0) {
            output.data[output.size++] = std::byte{byte}; // NOLINT
            break;
        }

        output.data[output.size++] =                      // NOLINT
            std::byte{static_cast<uint8_t>(byte | 0x80)}; // set continuation bit
    }

    return output;
}

constexpr std::expected<uint32_t, std::string> decode_leb128(ByteSpan input,
                                                             size_t& bytes_read) noexcept {
    uint32_t result = 0;
    int shift = 0;
    bytes_read = 0;

    for (auto b : input) {
        auto byte = static_cast<uint8_t>(b);
        bytes_read++;

        // is at 5th byte
        if (shift == 28) {
            // are _xxx ____ bits set
            if ((byte & 0x70) != 0) {
                return std::unexpected("Malformed LEB128: Value overflows uint32_t");
            }

            // is continuation bit set
            if ((byte & 0x80) != 0) {
                return std::unexpected("Malformed LEB128: Too many bytes for uint32_t");
            }
        }

        result |= static_cast<uint32_t>(byte & 0x7F) << shift;

        // is continuation bit set
        if ((byte & 0x80) == 0) {
            return result;
        }

        shift += 7;
    }

    return std::unexpected("Malformed LEB128: Unexpected end of input");
}

template <Serializable T>
std::expected<uint32_t, std::string> get_encoded_size(const T& value) noexcept {
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, ByteVec>) {
        return static_cast<uint32_t>(value.size());
    }

    // trivially copyables
    else {
        return static_cast<uint32_t>(sizeof(T));
    }
}

template <Serializable T>
std::expected<ByteVec, std::string> encode(const T& value) noexcept {
    const auto value_length_expected = get_encoded_size(value);
    if (!value_length_expected.has_value()) {
        return std::unexpected(value_length_expected.error());
    }
    const uint32_t value_length = *value_length_expected;
    const EncodedVarint length_prefix = encode_leb128(value_length);

    ByteVec buffer(length_prefix.size + value_length);
    std::memcpy(buffer.data(), length_prefix.data.data(), length_prefix.size);

    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, ByteVec>) {
        if (value_length > 0) {
            std::memcpy(buffer.data() + length_prefix.size, value.data(), value_length);
        }
    }

    else {
        std::memcpy(buffer.data() + length_prefix.size, &value, value_length);
    }

    return buffer;
}

template <Serializable T>
std::expected<T, std::string> decode(const ByteVec& encoded) noexcept {
    size_t length_prefix_size = 0;
    auto decoded_expected = decode_leb128(encoded, length_prefix_size);
    if (!decoded_expected.has_value()) {
        return std::unexpected(decoded_expected.error());
    }

    const uint32_t value_length = *decoded_expected;

    if (encoded.size() < length_prefix_size + value_length) [[unlikely]] {
        return std::unexpected("Encoded buffer is smaller than the declared length");
    }

    const std::byte* payload = encoded.data() + length_prefix_size;

    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, ByteVec>) {
        T buffer;
        buffer.resize(value_length);
        if (value_length > 0) {
            std::memcpy(buffer.data(), payload, value_length);
        }
        return buffer;
    }

    else {
        if (value_length != sizeof(T)) {
            return std::unexpected("Encoded length does not match the type size");
        }

        T buffer{};
        std::memcpy(&buffer, payload, sizeof(T));
        return buffer;
    }
}

template <Serializable T>
std::expected<ByteVec, std::string> serialize_value(const T& value) noexcept {
    if constexpr (std::is_same_v<T, std::string>) {
        ByteVec buffer(value.size());
        std::memcpy(buffer.data(), reinterpret_cast<const char*>(value.data()), value.size());
        return buffer;
    }

    else {
        ByteVec buffer(sizeof(value));
        std::memcpy(buffer.data(), reinterpret_cast<const char*>(&value), sizeof(value));
        return buffer;
    }
}

template <Serializable T>
std::expected<ByteSpan, std::string> serialize_view(const T& value) noexcept {
    if constexpr (std::is_same_v<T, std::string>) {
        return std::as_bytes(std::span{value});
    }

    else {
        // std::span{&value, 1}: makes it a single element span
        return std::as_bytes(std::span{&value, 1});
    }
}

// prevents passing rvalues, which will die when the function ends
template <Serializable T>
std::expected<ByteSpan, std::string> serialize_view(T&& value) = delete;

template <Serializable T>
std::expected<T, std::string> deserialize_value(const ByteVec& bytes,
                                                const uint32_t length) noexcept {
    if constexpr (std::is_same_v<T, std::string>) {
        std::string str(length, '\0');
        std::memcpy(str.data(), bytes.data(), str.size());
        return str;
    }

    else {
        T value;
        std::memcpy(&value, bytes.data(), sizeof(T));
        return value;
    }
}

template <typename T>
struct ViewOf {
    using type = const T*;
};

template <>
struct ViewOf<std::string> {
    using type = std::string_view;
};

template <typename T>
using view_of_t = ViewOf<T>::type;

template <typename T>
std::expected<view_of_t<T>, std::string> deserialize_view(ByteSpan bytes) noexcept {
    if constexpr (std::is_same_v<T, std::string>) {
        return std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    else {
        if (bytes.size() < sizeof(T)) {
            return std::unexpected("Buffer is smaller than the type size");
        }
        return reinterpret_cast<const T*>(bytes.data());
    }
}

template <Serializable T>
std::expected<view_of_t<T>, std::string> deserialize_view(const ByteVec& bytes) noexcept {
    return deserialize_view<T>(ByteSpan{bytes});
}

template <Serializable T>
std::expected<view_of_t<T>, std::string> deserialize_view(ByteVec&&) = delete;
