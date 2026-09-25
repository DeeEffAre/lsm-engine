#include "storage_engine/sstable.hpp"

#include <fstream>

SSTable::SSTable(std::string path) : path_(std::move(path)) {}

bool SSTable::get(const ByteVec& key, ByteVec& value) const noexcept {
    std::ifstream file(path_, std::ios::binary);

    if (!file.is_open()) {
        return false;
    }

    uint32_t key_len = 0;
    uint32_t val_len = 0;
    while (file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len))) {
        ByteVec iter_key(key_len);
        file.read(reinterpret_cast<char*>(iter_key.data()), key_len);

        file.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));

        if (iter_key == key) {
            value.resize(val_len);
            file.read(reinterpret_cast<char*>(value.data()), val_len);
            return true;
        }

        file.seekg(val_len, std::ios::cur); // skip on mismatch
    }

    return false;
}
