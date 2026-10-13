#include "storage_engine/engine.hpp"

#include "storage_engine/memtable.hpp"

#include <expected>
#include <fstream>

void Engine::put(ByteVec key, ByteVec value) noexcept {
    memtable_.put(std::move(key), std::move(value));
}

const ByteVec* Engine::get(ByteSpan key) const noexcept {
    return memtable_.get(key);
}

ByteVec* Engine::get(ByteSpan key) noexcept {
    return const_cast<ByteVec*>(std::as_const(memtable_).get(key)); // NOLINT
}

std::expected<SSTable, ErrorCode> Engine::write_sstable(std::string path) const noexcept {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(ErrorCode::FILE_ERROR);
    }

    if (!memtable_.write_to(file)) {
        return std::unexpected(ErrorCode::FILE_ERROR);
    }

    return SSTable(std::move(path));
};

void Engine::restore(Memtable&& table_to_load) noexcept {
    memtable_ = std::move(table_to_load);
}

void Engine::clear_memtable() noexcept {
    memtable_.clear();
}
