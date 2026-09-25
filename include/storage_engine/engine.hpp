#pragma once

#include "storage_engine/memtable.hpp"
#include "storage_engine/serialize_deserialize.hpp"
#include "storage_engine/sstable.hpp"
#include "storage_engine/types.hpp"

class Engine {
    Memtable memtable_;

public:
    void put(const ByteVec& key, ByteVec value) noexcept;

    // returns `nullptr` on error
    [[nodiscard]]
    const ByteVec* get(ByteSpan key) const noexcept;

    // returns `nullptr` on error
    ByteVec* get(ByteSpan key) noexcept;

    // returns `nullptr` on error
    [[nodiscard]]
    std::unique_ptr<SSTable> write_sstable(const std::string& path) const noexcept;
};

// an abstract layer for StorageEngine class with generic types
template <typename K, typename V>
class Database {
    Engine db_;

public:
    void put(const K& key, const V& value) noexcept {
        ByteVec key_encoded = serialize_value<K>(key);
        ByteVec value_encoded = serialize_value<V>(value);
        db_.put(key_encoded, value_encoded);
    }

    // returns `nullptr` on error
    [[nodiscard]]
    std::unique_ptr<V> get(const K& key) const noexcept {
        ByteSpan key_encoded = serialize_view<K>(key);

        const ByteVec* value_from_db = std::as_const(db_).get(key_encoded);
        if (value_from_db == nullptr) {
            return nullptr;
        }

        return std::make_unique<V>(
            deserialize_value<V>(*value_from_db, static_cast<uint32_t>(value_from_db->size())));
    }

    [[nodiscard]]
    Engine& get_db() noexcept {
        return db_;
    }
};
