#pragma once

#include "storage_engine/errors.hpp"
#include "storage_engine/memtable.hpp"
#include "storage_engine/serialize_deserialize.hpp"
#include "storage_engine/sstable.hpp"
#include "storage_engine/types.hpp"
#include "storage_engine/wal.hpp"

#include <expected>
#include <filesystem>
#include <ranges>

class Engine {
    Memtable memtable_;

public:
    void put(RecordType record_type, ByteVec key, ByteVec value) noexcept;

    [[nodiscard]]
    auto get(ByteSpan key) const noexcept -> std::expected<const ByteVec*, ErrorCode>;

    auto get(ByteSpan key) noexcept -> std::expected<ByteVec*, ErrorCode>;

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
    void clear_wal() noexcept { wal_.clear(); }

public:
    Database(size_t max_memtable_size) : wal_(wal_path_), max_memtable_size_(max_memtable_size) {
        namespace fs = std::filesystem;

        // load wal
        auto wal_replay = wal_.replay_wal();
        if (wal_replay.has_value()) {
            // update memtable size counter and memtable itself
            memtable_size_ = wal_replay->record_count;
            db_.restore(std::move(wal_replay->memtable));

            if (wal_replay->had_torn_tail) {
                wal_.truncate_to(wal_replay->bytes_consumed);
                std::println(stderr, "WAL: Torn tail truncated");
            }
        } else {
            std::println(stderr, "{}", to_string(wal_replay.error()));
        }

        // load sstables
        for (size_t index : std::views::iota(0U)) {
            // WARN: initialize that array so that fs::path constructor can determine where the
            // C-string ends
            std::array<char, 64> buffer{};

            std::format_to_n(buffer.data(), buffer.size(), "data-{}.sstable", index);
            fs::path current_file{buffer.data()};

            if (!fs::exists(current_file)) {
                break;
            }

            sstables_.emplace_back(std::move(current_file));
            sstable_counter_++;
        }
    }

    // TODO: maybe std::expected for explicitness

    constexpr bool handle(RecordType record_type, const K& key, const V& value) {
        switch (record_type) {
        case RecordType::PUT:
            return put(key, value);
        case RecordType::DELETE:
            return delete_record(key);
        default:
            [[unlikely]] return false;
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

        if (!wal_.write_put(*key_serialized, *value_serialized)) {
            return false;
        };

        db_.put(RecordType::PUT, std::move(*key_serialized), std::move(*value_serialized));
        memtable_size_++;

        if (memtable_size_ >= max_memtable_size_) {
            return flush_memtable();
        }

        return true;
    }

    bool delete_record(const K& key) noexcept {
        auto key_serialized = serialize_value(key);
        if (!key_serialized.has_value()) {
            return false;
        }

        if (!wal_.write_delete(*key_serialized)) {
            return false;
        }

        db_.put(RecordType::DELETE, std::move(*key_serialized), {});
        memtable_size_++;

        if (memtable_size_ >= max_memtable_size_) {
            return flush_memtable();
        }

        return true;
    }

    // TODO: again maybe wrap with std::expected for explicitness

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

        auto value_from_db_expected = std::as_const(db_).get(probe);
        if (!value_from_db_expected.has_value()) {
            if (value_from_db_expected.error() == ErrorCode::KEY_DELETED) {
                return nullptr;
            }

            // if ErrorCode::NOT_FOUND arrives, do nothing and search sstable
        } else {
            const auto* value_from_db = *value_from_db_expected;
            return decode_value(*value_from_db);
        }

        for (const auto& table : sstables_ | std::views::reverse) {
            auto result = table.get(probe);
            if (result.has_value()) {
                return decode_value(*result);
            }

            if (result.error() == ErrorCode::KEY_DELETED) {
                return nullptr;
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
