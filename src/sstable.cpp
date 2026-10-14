#include "storage_engine/sstable.hpp"

#include "storage_engine/length_encoding.hpp"
#include "storage_engine/types.hpp"

#include <cstring>
#include <expected>
#include <fstream>

std::expected<ByteVec, ErrorCode> SSTable::get(ByteSpan key) const noexcept {
    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(ErrorCode::FILE_ERROR);
    }

    StreamReader reader{file};

    while (true) {
        auto key_length = reader.read_varint();
        if (!key_length) {
            // At a record boundary with no bytes left -> normal "not found".
            // Anything else (UNEXPECTED_END, TOO_MANY_BYTES) means corruption.
            if (key_length.error() == ErrorCode::END_OF_STREAM) {
                break;
            }
            return std::unexpected(key_length.error());
        }

        auto stored_key = reader.read_bytes(*key_length);
        if (!stored_key) {
            return std::unexpected(stored_key.error());
        }

        auto value_length = reader.read_varint();
        if (!value_length) {
            return std::unexpected(value_length.error());
        }

        if (std::ranges::equal(*stored_key, key)) {
            auto payload = reader.read_bytes(*value_length);
            if (!payload) {
                return std::unexpected(payload.error());
            }
            return *payload;
        }

        if (!reader.skip(*value_length)) {
            return std::unexpected(ErrorCode::FILE_ERROR);
        }
    }

    return std::unexpected(ErrorCode::NOT_FOUND);
}
