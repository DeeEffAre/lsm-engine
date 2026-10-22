#include "storage_engine/sstable.hpp"

#include "storage_engine/errors.hpp"
#include "storage_engine/length_encoding.hpp"
#include "storage_engine/types.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fstream>
#include <optional>

namespace {
struct KeySection {
    uint32_t key_length;
    ByteVec key_payload;
};

struct ValueSection {
    uint32_t value_length;
    ByteVec value_payload;
};

inline auto read_key_section(StreamReader& reader) noexcept
    -> std::expected<KeySection, ErrorCode> {
    auto key_length = reader.read_varint();
    if (!key_length) {
        return std::unexpected(key_length.error());
    }

    auto stored_key = reader.read_bytes(*key_length);
    if (!stored_key) {
        return std::unexpected(stored_key.error());
    }

    return KeySection{.key_length = *key_length, .key_payload = std::move(*stored_key)};
}

inline auto read_value_section(StreamReader& reader) noexcept
    -> std::expected<ValueSection, ErrorCode> {
    auto value_length = reader.read_varint();
    if (!value_length) {
        return std::unexpected(value_length.error());
    }

    auto payload = reader.read_bytes(*value_length);
    if (!payload) {
        return std::unexpected(payload.error());
    }

    return ValueSection{.value_length = *value_length, .value_payload = std::move(*payload)};
}

inline auto skip_over_value_section(StreamReader& reader) noexcept -> std::optional<ErrorCode> {
    auto value_length = reader.read_varint();
    if (!value_length) {
        return value_length.error();
    }

    if (!reader.skip(*value_length)) {
        return ErrorCode::FILE_ERROR;
    }

    return std::nullopt;
}
} // namespace

auto SSTable::get(ByteSpan key) const noexcept -> std::expected<ByteVec, ErrorCode> {
    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) {
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

        const auto key_section = read_key_section(reader);
        if (!key_section.has_value()) {
            return std::unexpected(key_section.error());
        }

        const auto [key_length, key_payload] = *key_section;

        if (std::ranges::equal(key_payload, key)) {
            if (record_type == RecordType::DELETE) {
                return std::unexpected(ErrorCode::KEY_DELETED);
            }

            // PUT record
            const auto value_section = read_value_section(reader);
            if (!value_section.has_value()) {
                return std::unexpected(value_section.error());
            }

            const auto [value_length, value_payload] = *value_section;

            return value_payload;
        }

        // not the key we are searching for
        // if it is a DELETE record than it has no value section, continue to iterate
        // if it is a PUT record than we must skip over the value section
        if (record_type == RecordType::PUT) {
            if (auto result = skip_over_value_section(reader); result.has_value()) {
                return std::unexpected(*result);
            }
        }
    }

    return std::unexpected(ErrorCode::NOT_FOUND);
}
