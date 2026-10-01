#include "storage_engine/sstable.hpp"

#include "storage_engine/serialize_deserialize.hpp"

#include <cstring>
#include <expected>
#include <fstream>

SSTable::SSTable(std::string path) : path_(std::move(path)) {}

std::expected<ByteVec, SSTableError> SSTable::get(ByteSpan key) const noexcept {
    std::ifstream file(path_, std::ios::binary);

    if (!file.is_open()) {
        return std::unexpected(SSTableError::FILE_ERROR);
    }

    constexpr size_t LENGTH_PREFIX_LENGTH = sizeof(uint32_t);

    while (true) {
        uint32_t key_length = 0;
        if (!file.read(reinterpret_cast<char*>(&key_length), LENGTH_PREFIX_LENGTH)) {
            break;
        }

        // Rebuild the encoded key record (length prefix + payload) so it can be decoded.
        ByteVec encoded_key(LENGTH_PREFIX_LENGTH + key_length);
        std::memcpy(encoded_key.data(), &key_length, LENGTH_PREFIX_LENGTH);
        if (!file.read(reinterpret_cast<char*>(encoded_key.data() + LENGTH_PREFIX_LENGTH),
                       key_length)) {
            return std::unexpected(SSTableError::FILE_ERROR);
        }

        uint32_t value_length = 0;
        if (!file.read(reinterpret_cast<char*>(&value_length), LENGTH_PREFIX_LENGTH)) {
            return std::unexpected(SSTableError::FILE_ERROR);
        }

        auto stored_key = decode<ByteVec>(encoded_key);
        if (!stored_key.has_value()) {
            return std::unexpected(SSTableError::FILE_ERROR);
        }

        if (std::ranges::equal(*stored_key, key)) {
            ByteVec encoded_value(LENGTH_PREFIX_LENGTH + value_length);
            std::memcpy(encoded_value.data(), &value_length, LENGTH_PREFIX_LENGTH);
            if (!file.read(reinterpret_cast<char*>(encoded_value.data() + LENGTH_PREFIX_LENGTH),
                           value_length)) {
                return std::unexpected(SSTableError::FILE_ERROR);
            }
            return encoded_value;
        }

        file.seekg(value_length, std::ios::cur); // skip on mismatch
    }

    return std::unexpected(SSTableError::NOT_FOUND);
}
