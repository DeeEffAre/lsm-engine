#pragma once

#include "storage_engine/memtable.hpp"
#include "storage_engine/serialize_deserialize.hpp"
#include "storage_engine/sstable.hpp"
#include "storage_engine/types.hpp"
#include "storage_engine/wal.hpp"

#include <expected>
#include <ranges>

class Engine {
    Memtable memtable_;

public:
    void put(ByteVec key, ByteVec value) noexcept;

    // returns `nullptr` on error
    [[nodiscard]]
    const ByteVec* get(ByteSpan key) const noexcept;

    // returns `nullptr` on error
    ByteVec* get(ByteSpan key) noexcept;

    [[nodiscard]]
    std::expected<SSTable, ErrorCode> write_sstable(const char* path) const noexcept;

    void restore(Memtable&& table_to_load) noexcept;

    void clear_memtable() noexcept;
};

// an abstract layer for StorageEngine class with generic types
template <typename K, typename V>
class Database {
    std::vector<SSTable> sstables_;
    std::string wal_path_{"db.wal"};
    Wal wal_;
    Engine db_{};
    size_t max_memtable_size_;
    size_t memtable_size_{0};
    size_t sstable_counter_{0};

    void clear_memtable() noexcept { db_.clear_memtable(); }
    void clear_wal() noexcept { wal_.clear(wal_path_); }

public:
    Database(size_t max_memtable_size) : wal_(wal_path_), max_memtable_size_(max_memtable_size) {
        auto wal_replay = Wal::replay_wal(wal_path_);

        if (wal_replay.has_value()) {
            // update memtable size counter and memtable itself
            memtable_size_ = wal_replay->record_count;
            db_.restore(std::move(wal_replay->memtable));
        } else {
            std::println(stderr, "{}", to_string(wal_replay.error()));
        }
    }

    bool put(const K& key, const V& value) noexcept {
        auto value_serialized = serialize_value<V>(value);
        if (!value_serialized.has_value()) {
            return false;
        }

        auto key_serialized = serialize_value<K>(key);
        if (!key_serialized.has_value()) {
            return false;
        }

        if (!wal_.write(*key_serialized, *value_serialized)) {
            return false;
        };

        db_.put(std::move(*key_serialized), std::move(*value_serialized));
        memtable_size_++;

        if (memtable_size_ >= max_memtable_size_) {
            return flush_memtable();
        }

        return true;
    }

    // returns `nullptr` on error
    [[nodiscard]]
    std::unique_ptr<V> get(const K& key) const noexcept {
        const auto probe_expected = serialize_view<K>(key);
        if (!probe_expected.has_value()) {
            return nullptr;
        }
        const ByteSpan probe = *probe_expected;

        const auto decode_value = [](const ByteVec& bytes) -> std::unique_ptr<V> {
            auto decoded = deserialize_value<V>(bytes, static_cast<uint32_t>(bytes.size()));
            if (!decoded.has_value()) {
                return nullptr;
            }
            return std::make_unique<V>(std::move(*decoded));
        };

        const ByteVec* value_from_db = std::as_const(db_).get(probe);
        if (value_from_db != nullptr) {
            return decode_value(*value_from_db);
        }

        for (const auto& table : sstables_ | std::views::reverse) {
            auto result = table.get(probe);
            if (result.has_value()) {
                return decode_value(*result);
            }

            if (result.error() == ErrorCode::FILE_ERROR) {
                // NOTE: quitting from function may not be the best choice when a file error
                // occurrs.
                return nullptr;
            }
        }

        return nullptr;
    }

    bool flush_memtable() noexcept {
        // std::format alternative which does not throws an exception
        std::array<char, 64> buffer{};
        std::format_to_n(buffer.data(), buffer.size(), "data-{}.sstable", sstable_counter_);

        auto sstable = db_.write_sstable(buffer.data());

        if (!sstable.has_value()) {
            std::println(stderr, "Error while flushing memtable: {}", to_string(sstable.error()));
            return false;
        }

        sstables_.push_back(*sstable);
        sstable_counter_++;
        memtable_size_ = 0;
        clear_memtable();
        clear_wal();

        return true;
    }

    [[nodiscard]]
    Engine& get_db() noexcept {
        return db_;
    }
};
