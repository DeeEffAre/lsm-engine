#include "storage_engine/engine.hpp"

#include "storage_engine/memtable.hpp"

#include <expected>
#include <fstream>

void Engine::put(RecordType record_type, ByteVec key, ByteVec value) noexcept {
    memtable_.put(record_type, std::move(key), std::move(value));
}

auto Engine::get(ByteSpan key) const noexcept -> std::expected<const ByteVec*, ErrorCode> {
    return memtable_.get(key);
}

auto Engine::get(ByteSpan key) noexcept -> std::expected<ByteVec*, ErrorCode> {
    return std::as_const(memtable_).get(key).transform(
        [](auto inner) { return const_cast<ByteVec*>(inner); }); // NOLINT
}

std::expected<SSTable, ErrorCode> Engine::write_sstable(const char* path) const noexcept {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(ErrorCode::FILE_ERROR);
    }

    if (!memtable_.write_to(file)) {
        return std::unexpected(ErrorCode::FILE_ERROR);
    }

    return SSTable(path);
};

void Engine::restore(Memtable&& table_to_load) noexcept {
    memtable_ = std::move(table_to_load);
}

void Engine::clear_memtable() noexcept {
    memtable_.clear();
}
