#pragma once

#include "storage_engine/types.hpp"

#include <cassert>
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

template <Serializable T>
uint32_t get_encoded_size(const T& value) {
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, ByteVec>) {
        return static_cast<uint32_t>(value.size());
    }

    else if constexpr (std::is_trivially_copyable_v<T>) {
        return static_cast<uint32_t>(sizeof(T));
    }

    else {
        static_assert(sizeof(T) == 0, "Cannot calculate the length for that type!");
    }
}

template <Serializable T>
std::expected<ByteVec, std::string> encode(const T& value) {
    const uint32_t value_length = get_encoded_size(value);
    constexpr size_t LENGTH_PREFIX_LENGTH = sizeof(uint32_t);

    ByteVec buffer(LENGTH_PREFIX_LENGTH + value_length);
    std::memcpy(buffer.data(), &value_length, LENGTH_PREFIX_LENGTH);

    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, ByteVec>) {
        if (value_length > 0) {
            std::memcpy(buffer.data() + LENGTH_PREFIX_LENGTH, value.data(), value_length);
        }
    } else if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(buffer.data() + LENGTH_PREFIX_LENGTH, &value, value_length);
    } else {
        return std::unexpected("No encoding described for that type!");
    }

    return buffer;
}

template <Serializable T>
std::expected<T, std::string> decode(const ByteVec& encoded) {
    constexpr size_t LENGTH_PREFIX_LENGTH = sizeof(uint32_t);

    if (encoded.size() < LENGTH_PREFIX_LENGTH) [[unlikely]] {
        return std::unexpected("Encoded buffer is smaller than the length prefix");
    }

    uint32_t value_length = 0;
    std::memcpy(&value_length, encoded.data(), LENGTH_PREFIX_LENGTH);

    if (encoded.size() < LENGTH_PREFIX_LENGTH + value_length) [[unlikely]] {
        return std::unexpected("Encoded buffer is smaller than the declared length");
    }

    const std::byte* payload = encoded.data() + LENGTH_PREFIX_LENGTH;

    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, ByteVec>) {
        T buffer;
        buffer.resize(value_length);
        if (value_length > 0) {
            std::memcpy(buffer.data(), payload, value_length);
        }
        return buffer;
    } else if constexpr (std::is_trivially_copyable_v<T>) {
        if (value_length != sizeof(T)) {
            return std::unexpected("Encoded length does not match the type size");
        }

        T buffer{};
        std::memcpy(&buffer, payload, sizeof(T));
        return buffer;
    } else {
        return std::unexpected("No decoding described for that type!");
    }
}

template <Serializable T>
ByteVec serialize_value(const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        ByteVec buffer(value.size());
        std::memcpy(buffer.data(), reinterpret_cast<const char*>(value.data()), value.size());
        return buffer;
    }

    else if constexpr (std::is_trivially_copyable_v<T>) {
        ByteVec buffer(sizeof(value));
        std::memcpy(buffer.data(), reinterpret_cast<const char*>(&value), sizeof(value));
        return buffer;
    }

    else {
        static_assert(sizeof(T) == 0, "No serialization described for that type!");
    }
}

template <Serializable T>
ByteSpan serialize_view(const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        return std::as_bytes(std::span{value});
    }

    else if constexpr (std::is_trivially_copyable_v<T>) {
        // std::span{&value, 1}: makes it a single element span
        return std::as_bytes(std::span{&value, 1});
    }
}

// prevents passing rvalues, which will die when the function ends
template <Serializable T>
ByteSpan serialize_view(T&& value) = delete;

// TODO: zero copy deserialization maybe
template <Serializable T>
T deserialize_value(const ByteVec& bytes, const uint32_t length) {
    if constexpr (std::is_same_v<T, std::string>) {
        std::string str(length, '\0');
        std::memcpy(str.data(), bytes.data(), str.size());
        return str;
    }

    else if constexpr (std::is_trivially_copyable_v<T>) {
        T value;
        std::memcpy(&value, bytes.data(), sizeof(T));
        return value;
    }

    else {
        static_assert(sizeof(T) == 0, "No deserialization described for that type!");
    }
}

template <typename T>
struct ViewOf {
    using type = const T&;
};

template <>
struct ViewOf<std::string> {
    using type = std::string_view;
};

template <typename T>
using view_of_t = ViewOf<T>::type;

template <typename T>
view_of_t<T> deserialize_view(ByteSpan bytes) {
    if constexpr (std::is_same_v<T, std::string>) {
        return std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    else if constexpr (std::is_trivially_copyable_v<T>) {
        assert(bytes.size() >= sizeof(T));
        return *reinterpret_cast<const T*>(bytes.data());
    }
}

template <Serializable T>
view_of_t<T> deserialize_view(const ByteVec& bytes) {
    return deserialize_view<T>(ByteSpan{bytes});
}

template <Serializable T>
view_of_t<T> deserialize_view(ByteVec&&) = delete;
