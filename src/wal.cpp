#include "storage_engine/wal.hpp"

#include "storage_engine/length_encoding.hpp"

std::expected<WalReplayResult, ErrorCode> Wal::replay_wal(const std::string& path) noexcept {
    WalReplayResult result{};

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) [[unlikely]] {
        return std::unexpected(ErrorCode::FILE_ERROR);
    }

    StreamReader reader{file};

    while (true) {
        auto key_length = reader.read_varint();
        if (!key_length) [[unlikely]] {
            if (key_length.error() == ErrorCode::END_OF_STREAM) {
                break;
            }
            return std::unexpected(key_length.error());
        }

        auto stored_key = reader.read_bytes(*key_length);
        if (!stored_key) [[unlikely]] {
            return std::unexpected(stored_key.error());
        }

        auto value_length = reader.read_varint();
        if (!value_length) [[unlikely]] {
            return std::unexpected(value_length.error());
        }

        auto stored_value = reader.read_bytes(*value_length);
        if (!stored_value) [[unlikely]] {
            return std::unexpected(stored_value.error());
        }

        result.memtable.put(std::move(*stored_key), std::move(*stored_value));
        result.record_count++;
    }

    return result;
}
