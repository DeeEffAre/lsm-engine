#include "storage_engine/engine.hpp"

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

std::unique_ptr<SSTable> Engine::write_sstable(const std::string& path) const noexcept {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return nullptr;
    }

    if (!memtable_.write_to(file)) {
        return nullptr;
    }

    return std::make_unique<SSTable>(path);
};
