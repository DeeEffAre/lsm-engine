#pragma once

#include <fstream>

// Formula:
// $length (4 byte) + $value ($length)

template <typename T> uint32_t get_encoded_size(const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        return static_cast<uint32_t>(value.size());
    } else if constexpr (std::is_trivially_copyable_v<T>) {
        return static_cast<uint32_t>(sizeof(T));
    } else {
        static_assert(sizeof(T) == 0, "Cannot calculate the length for that type!");
    }
}

template <typename T> void serialize_value(std::ofstream& file, const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        file.write(value.data(), static_cast<long>(value.size()));
    } else if constexpr (std::is_trivially_copyable_v<T>) {
        file.write(reinterpret_cast<const char*>(&value), sizeof(T));
    } else {
        static_assert(sizeof(T) == 0, "No serialization described for that type!");
    }
}

template <typename T> T deserialize_value(std::ifstream& file, uint32_t length) {
    if constexpr (std::is_same_v<T, std::string>) {
        std::string str(length, '\0');
        file.read(str.data(), length);
        return str;
    } else if constexpr (std::is_trivially_copyable_v<T>) {
        T value{};
        file.read(reinterpret_cast<char*>(&value), sizeof(T));
        return value;
    } else {
        static_assert(sizeof(T) == 0, "No deserialization described for that type!");
    }
}
