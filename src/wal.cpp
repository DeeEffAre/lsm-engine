#include "storage_engine/wal.hpp"

#include "storage_engine/errors.hpp"
#include "storage_engine/length_encoding.hpp"

auto Wal::replay_wal() const noexcept -> std::expected<WalReplayResult, ErrorCode> {
    WalReplayResult result{};

    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) [[unlikely]] {
        return std::unexpected(ErrorCode::FILE_ERROR);
    }

    StreamReader reader{file};

    while (true) {
        const auto record_type_optional = reader.read_record_type();

        // EOF
        if (!record_type_optional.has_value()) {
            break;
        }
        const auto record_type = *record_type_optional;

        auto key_length = reader.read_varint();
        if (!key_length) [[unlikely]] {
            // because there is record type before key section now, 0 size key length after a record
            // type should be considered as torn-tail
            if (key_length.error() == ErrorCode::END_OF_STREAM) {
                result.had_torn_tail = true;
                break;
            }

            // crash at mid record
            if (key_length.error() == ErrorCode::UNEXPECTED_END) {
                result.had_torn_tail = true;
                break;
            }

            return std::unexpected(key_length.error()); // corrupted data
        }

        auto stored_key = reader.read_bytes(*key_length);
        if (!stored_key) [[unlikely]] {
            if (stored_key.error() == ErrorCode::NOT_ENOUGH_BYTES_LEFT) {
                result.had_torn_tail = true;
                break;
            }

            return std::unexpected(stored_key.error());
        }

        // there is no value section so skip over
        if (record_type == RecordType::DELETE) {
            result.memtable.put(record_type, std::move(*stored_key), {});
            result.record_count++;
            result.bytes_consumed = static_cast<size_t>(file.tellg());
            continue;
        }

        auto value_length = reader.read_varint();
        if (!value_length) [[unlikely]] {
            if (value_length.error() == ErrorCode::END_OF_STREAM) {
                result.had_torn_tail = true;
                break;
            }

            // crash at mid record
            if (value_length.error() == ErrorCode::UNEXPECTED_END) {
                result.had_torn_tail = true;
                break;
            }

            return std::unexpected(value_length.error()); // corrupted data
        }

        auto stored_value = reader.read_bytes(*value_length);
        if (!stored_value) [[unlikely]] {
            if (stored_value.error() == ErrorCode::NOT_ENOUGH_BYTES_LEFT) {
                result.had_torn_tail = true;
                break;
            }

            return std::unexpected(stored_value.error());
        }

        result.memtable.put(record_type, std::move(*stored_key), std::move(*stored_value));
        result.record_count++;

        // record last healthy position
        result.bytes_consumed = static_cast<size_t>(file.tellg());
    }

    return result;
}
