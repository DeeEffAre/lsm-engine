#include "storage_engine/sstable.hpp"

#include "storage_engine/serialize_deserialize.hpp"
#include "storage_engine/types.hpp"

#include <array>
#include <cstring>
#include <expected>
#include <fstream>
#include <optional>

namespace {

struct LengthPrefix {
    std::array<std::byte, 5> bytes{};
    size_t size{0};
    uint32_t value{0};
};

std::optional<LengthPrefix> read_length_prefix(std::istream& file) noexcept {
    LengthPrefix prefix;

    for (size_t i = 0; i < prefix.bytes.size(); ++i) {
        char raw_byte = 0;
        if (!file.get(raw_byte)) {
            return std::nullopt;
        }

        const auto byte = static_cast<uint8_t>(raw_byte);
        prefix.bytes[i] = static_cast<std::byte>(byte); // NOLINT
        prefix.size++;

        // continuation bit clear: the prefix ends here
        if ((byte & 0x80) == 0) {
            size_t bytes_read = 0;
            auto decoded = decode_leb128(ByteSpan{prefix.bytes}.first(prefix.size), bytes_read);
            if (!decoded.has_value()) {
                return std::nullopt;
            }
            prefix.value = *decoded;
            return prefix;
        }
    }

    return std::nullopt; // more than 5 bytes: malformed
}

} // namespace

SSTable::SSTable(std::string path) : path_(std::move(path)) {}

std::expected<ByteVec, SSTableError> SSTable::get(ByteSpan key) const noexcept {
    std::ifstream file(path_, std::ios::binary);

    if (!file.is_open()) {
        return std::unexpected(SSTableError::FILE_ERROR);
    }

    while (true) {
        auto key_prefix = read_length_prefix(file);
        if (!key_prefix.has_value()) {
            break; // clean end of file (or malformed prefix)
        }

        ByteVec encoded_key(key_prefix->size + key_prefix->value);
        std::memcpy(encoded_key.data(), key_prefix->bytes.data(), key_prefix->size);
        if (!file.read(reinterpret_cast<char*>(encoded_key.data() + key_prefix->size),
                       key_prefix->value)) {
            return std::unexpected(SSTableError::FILE_ERROR);
        }

        auto value_prefix = read_length_prefix(file);
        if (!value_prefix.has_value()) {
            return std::unexpected(SSTableError::FILE_ERROR);
        }

        auto stored_key = decode<ByteVec>(encoded_key);
        if (!stored_key.has_value()) {
            return std::unexpected(SSTableError::FILE_ERROR);
        }

        if (std::ranges::equal(*stored_key, key)) {
            ByteVec encoded_value(value_prefix->size + value_prefix->value);
            std::memcpy(encoded_value.data(), value_prefix->bytes.data(), value_prefix->size);
            if (!file.read(reinterpret_cast<char*>(encoded_value.data() + value_prefix->size),
                           value_prefix->value)) {
                return std::unexpected(SSTableError::FILE_ERROR);
            }
            return encoded_value;
        }

        file.seekg(value_prefix->value, std::ios::cur); // skip on mismatch
    }

    return std::unexpected(SSTableError::NOT_FOUND);
}
