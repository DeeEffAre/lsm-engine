#pragma once

#include <cstdint>
#include <string_view>

enum class ErrorCode : uint8_t {
    NOT_FOUND,             // requested key does not exist
    FILE_ERROR,            // file could not be opened, read or written
    END_OF_STREAM,         // clean end: stream ended at a record boundary (not an error)
    UNEXPECTED_END,        // stream ended in the middle of a varint/payload (corruption)
    TOO_MANY_BYTES,        // LEB128 prefix is longer than 5 bytes
    OVERFLOW,              // decoded value does not fit into uint32_t
    BUFFER_TOO_SMALL,      // decode: declared length exceeds the buffer size
    LENGTH_MISMATCH,       // trivially copyable type size does not match the encoded length
    NOT_ENOUGH_BYTES_LEFT, // reader was asked for more bytes than are left
    KEY_DELETED,           // key has been tombstoned
};

constexpr std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::NOT_FOUND:
        return "not found";
    case ErrorCode::FILE_ERROR:
        return "file error";
    case ErrorCode::END_OF_STREAM:
        return "end of stream";
    case ErrorCode::UNEXPECTED_END:
        return "unexpected end of input";
    case ErrorCode::TOO_MANY_BYTES:
        return "too many bytes";
    case ErrorCode::OVERFLOW:
        return "value overflow";
    case ErrorCode::BUFFER_TOO_SMALL:
        return "buffer too small";
    case ErrorCode::LENGTH_MISMATCH:
        return "length mismatch";
    case ErrorCode::NOT_ENOUGH_BYTES_LEFT:
        return "not enough bytes left";
    case ErrorCode::KEY_DELETED:
        return "this key has been deleted";
    }
    return "unknown";
}
