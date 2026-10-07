#include "storage_engine/length_encoding.hpp"

#include "storage_engine/errors.hpp"

#include <array>
#include <expected>

std::expected<uint32_t, ErrorCode> ByteReader::read_varint() {
    if (at_end()) {
        return std::unexpected(ErrorCode::END_OF_STREAM);
    }

    auto decoded = decode_leb128(bytes_.subspan(pos_));
    if (!decoded.has_value()) {
        return std::unexpected(decoded.error());
    }

    pos_ += decoded->bytes_read;
    return decoded->value;
}

std::expected<ByteSpan, ErrorCode> ByteReader::read_bytes(size_t payload_length) {
    if (remaining() < payload_length) {
        return std::unexpected(ErrorCode::NOT_ENOUGH_BYTES_LEFT);
    }

    const ByteSpan result = bytes_.subspan(pos_, payload_length);
    pos_ += payload_length;

    return result;
}

std::expected<uint32_t, ErrorCode> StreamReader::read_varint() {
    std::array<std::byte, 5> buffer{};
    size_t size = 0;

    for (size_t i = 0; i < buffer.size(); ++i) {
        char raw_byte = 0;
        if (!file_.get(raw_byte)) {
            return std::unexpected(size == 0 ? ErrorCode::END_OF_STREAM
                                             : ErrorCode::UNEXPECTED_END);
        }

        const auto byte = static_cast<uint8_t>(raw_byte);
        buffer[i] = static_cast<std::byte>(byte); // NOLINT
        size++;

        if ((byte & 0x80) == 0) {
            auto decoded = decode_leb128(ByteSpan{buffer}.first(size));
            if (!decoded.has_value()) {
                return std::unexpected(decoded.error());
            }
            return decoded->value;
        }
    }

    return std::unexpected(ErrorCode::TOO_MANY_BYTES);
}

std::expected<ByteVec, ErrorCode> StreamReader::read_bytes(size_t payload_length) {
    ByteVec buffer(payload_length);
    file_.read(reinterpret_cast<char*>(buffer.data()),
               static_cast<std::streamsize>(payload_length));

    if (file_.gcount() != static_cast<std::streamsize>(payload_length)) {
        return std::unexpected(ErrorCode::NOT_ENOUGH_BYTES_LEFT);
    }

    return buffer;
}

std::expected<void, ErrorCode> StreamReader::skip(size_t length) {
    file_.seekg(static_cast<std::streamoff>(length), std::ios::cur);
    if (!file_) {
        return std::unexpected(ErrorCode::FILE_ERROR);
    }
    return {};
}
