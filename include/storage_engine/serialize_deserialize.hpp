#pragma once

#include <cstring>
#include <span>

// Formula:
// $length (4 byte) + $value ($length)

// supported types:
// - is_trivially_copyable types (e.g. uint32_t, int)
// - contiguous containers and types (e.g. std::string, std::vector<int>)

using ByteVec = std::vector<std::byte>;
using ByteSpan = std::span<const std::byte>;

// TODO: generic contiguous container support (e.g. std::vector<T>)
template <typename T>
concept Serializable = std::is_trivially_copyable_v<T> || std::is_same_v<T, std::string>;

template <Serializable T>
uint32_t get_encoded_size(const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
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
