#pragma once

#include "storage_engine/errors.hpp"
#include "storage_engine/length_encoding.hpp"
#include "storage_engine/types.hpp"

#include <cstdint>
#include <cstring>
#include <expected>
#include <span>

// Formula:
// $length (varint, max 4bytes) + $value ($length byte)

// TODO: generic contiguous container support (e.g. std::vector<T>)
template <typename T>
concept Serializable =
    std::is_trivially_copyable_v<T> || std::is_same_v<T, std::string> || std::is_same_v<T, ByteVec>;

template <Serializable T>
std::expected<uint32_t, ErrorCode> get_encoded_size(const T& value) noexcept {
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, ByteVec>) {
        return static_cast<uint32_t>(value.size());
    }

    // trivially copyables
    else {
        return static_cast<uint32_t>(sizeof(T));
    }
}

template <Serializable T>
std::expected<ByteVec, ErrorCode> encode(const T& value) noexcept {
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
std::expected<T, ErrorCode> decode(const ByteVec& encoded) noexcept {
    ByteReader reader(encoded);

    auto value_length = reader.read_varint();
    if (!value_length.has_value()) {
        return std::unexpected(value_length.error());
    }

    auto payload = reader.read_bytes(*value_length);
    if (!payload.has_value()) {
        return std::unexpected(payload.error());
    }

    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, ByteVec>) {
        T buffer;
        buffer.resize(payload->size());
        if (!payload->empty()) {
            std::memcpy(buffer.data(), payload->data(), payload->size());
        }
        return buffer;
    }

    else {
        if (*value_length != sizeof(T)) {
            return std::unexpected(ErrorCode::LENGTH_MISMATCH);
        }

        T buffer{};
        std::memcpy(&buffer, payload->data(), sizeof(T));
        return buffer;
    }
}

template <Serializable T>
std::expected<ByteVec, ErrorCode> serialize_value(const T& value) noexcept {
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
std::expected<ByteSpan, ErrorCode> serialize_view(const T& value) noexcept {
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
std::expected<ByteSpan, ErrorCode> serialize_view(T&& value) = delete;

template <Serializable T>
std::expected<T, ErrorCode> deserialize_value(const ByteVec& bytes,
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
std::expected<view_of_t<T>, ErrorCode> deserialize_view(ByteSpan bytes) noexcept {
    if constexpr (std::is_same_v<T, std::string>) {
        return std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    else {
        if (bytes.size() < sizeof(T)) {
            return std::unexpected(ErrorCode::BUFFER_TOO_SMALL);
        }
        return reinterpret_cast<const T*>(bytes.data());
    }
}

template <Serializable T>
std::expected<view_of_t<T>, ErrorCode> deserialize_view(const ByteVec& bytes) noexcept {
    return deserialize_view<T>(ByteSpan{bytes});
}

template <Serializable T>
std::expected<view_of_t<T>, ErrorCode> deserialize_view(ByteVec&&) = delete;
