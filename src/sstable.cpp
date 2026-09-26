#include "storage_engine/sstable.hpp"

#include <expected>
#include <fstream>

SSTable::SSTable(std::string path) : path_(std::move(path)) {}

std::expected<ByteVec, SSTableError> SSTable::get(ByteSpan key) const noexcept {
    std::ifstream file(path_, std::ios::binary);

    if (!file.is_open()) {
        return std::unexpected(SSTableError::FILE_ERROR);
    }

    uint32_t key_len = 0;
    uint32_t val_len = 0;
    while (file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len))) {
        ByteVec iter_key(key_len);
        file.read(reinterpret_cast<char*>(iter_key.data()), key_len);

        file.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));

        if (std::ranges::equal(iter_key, key)) {
            std::vector<std::byte> buffer;
            buffer.resize(val_len);
            file.read(reinterpret_cast<char*>(buffer.data()), val_len);
            return buffer;
        }

        file.seekg(val_len, std::ios::cur); // skip on mismatch
    }

    return std::unexpected(SSTableError::NOT_FOUND);
}
