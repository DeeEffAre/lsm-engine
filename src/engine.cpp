#include "storage_engine/engine.hpp"

#include <expected>
#include <fstream>

void Engine::put(const ByteVec& key, ByteVec value) noexcept {
    memtable_.put(key, std::move(value));
}

const ByteVec* Engine::get(ByteSpan key) const noexcept {
    return memtable_.get(key);
}

ByteVec* Engine::get(ByteSpan key) noexcept {
    return const_cast<ByteVec*>(std::as_const(memtable_).get(key)); // NOLINT
}

std::expected<SSTable, std::string> Engine::write_sstable(std::string path) const noexcept {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected("Failed to open file");
    }

    if (!memtable_.write_to(file)) {
        return std::unexpected("Failed to write to file");
    }

    return SSTable(std::move(path));
};
