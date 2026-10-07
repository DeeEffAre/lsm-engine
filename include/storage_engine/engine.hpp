#pragma once

#include "storage_engine/memtable.hpp"
#include "storage_engine/serialize_deserialize.hpp"
#include "storage_engine/sstable.hpp"
#include "storage_engine/types.hpp"

#include <expected>
#include <ranges>

class Engine {
    Memtable memtable_;

public:
    void put(const ByteVec& key, ByteVec value) noexcept;

    // returns `nullptr` on error
    [[nodiscard]]
    const ByteVec* get(ByteSpan key) const noexcept;

    // returns `nullptr` on error
    ByteVec* get(ByteSpan key) noexcept;

    [[nodiscard]]
    std::expected<SSTable, ErrorCode> write_sstable(std::string path) const noexcept;

    void clear_memtable() noexcept;
};

// an abstract layer for StorageEngine class with generic types
template <typename K, typename V>
class Database {
    Engine db_{};
    std::vector<SSTable> sstables_;
    int max_memtable_size_;
    int memtable_size_{0};
    int sstable_counter_{0};

public:
    Database(int max_memtable_size) : max_memtable_size_(max_memtable_size) {}

    bool put(const K& key, const V& value) noexcept {
        auto value_encoded = encode<V>(value);
        if (!value_encoded.has_value()) {
            return false;
        }

        auto key_encoded = serialize_value<K>(key);
        if (!key_encoded.has_value()) {
            return false;
        }

        db_.put(*key_encoded, std::move(*value_encoded));
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
            auto decoded = decode<V>(bytes);
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

    void clear_memtable() noexcept { db_.clear_memtable(); }

    bool flush_memtable() noexcept {
        std::string sstable_path = std::format("data-{}.sstable", sstable_counter_);
        auto sstable = db_.write_sstable(std::move(sstable_path));

        if (!sstable.has_value()) {
            std::println(stderr, "Error while flushing memtable: {}", to_string(sstable.error()));
            return false;
        }

        sstables_.push_back(*sstable);
        sstable_counter_++;
        memtable_size_ = 0;
        clear_memtable();

        return true;
    }

    [[nodiscard]]
    Engine& get_db() noexcept {
        return db_;
    }
};
