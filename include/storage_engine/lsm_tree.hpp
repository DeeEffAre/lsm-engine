#pragma once

#include "serialize_deserialize.hpp"

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <utility>

using ByteVec = std::vector<std::byte>;
using ByteSpan = std::span<const std::byte>;

struct ByteCompare {
    using is_transparent = void; // enables heterogeneous lookup

    // basic_string_view<std::byte> has optimizations like hardware level `memcmp` call
    static constexpr std::basic_string_view<std::byte> to_view(ByteSpan span) noexcept {
        return {span.data(), span.size()};
    }

    static constexpr std::basic_string_view<std::byte> to_view(const ByteVec& vec) noexcept {
        return {vec.data(), vec.size()};
    }

    // spaceship operator (<=>) generates six comparisons at once (<, <=, >, >=, ==, and !=)
    template <typename T, typename U>
    bool operator()(const T& lhs, const U& rhs) const noexcept {
        return (to_view(lhs) <=> to_view(rhs)) < 0;
    }
};

class Memtable {
    // TODO: change this with to a custom lock free SkipList
    std::map<ByteVec, ByteVec, ByteCompare> data_;

public:
    void put(const ByteVec& key, ByteVec value) noexcept { data_.insert_or_assign(key, value); }

    // returns `nullptr` on error
    [[nodiscard]]
    const ByteVec* get(ByteSpan key) const noexcept {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return nullptr;
        }
        return &(it->second);
    }

    // returns `nullptr` on error
    ByteVec* get(ByteSpan key) noexcept {
        return const_cast<ByteVec*>(std::as_const(*this).get(key)); // NOLINT
    }

    [[nodiscard]]
    auto data() const noexcept -> const std::map<ByteVec, ByteVec, ByteCompare>& {
        return data_;
    }
};

class SSTable {
    std::string path_;

public:
    SSTable(std::string path) : path_(std::move(path)) {}

    // returns `false` on error
    [[nodiscard]]
    bool get(const ByteVec& key, ByteVec& value) const noexcept {
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
};

class StorageEngine {
    Memtable memtable_{};

public:
    void put(const ByteVec& key, ByteVec value) noexcept { memtable_.put(key, std::move(value)); }

    // returns `nullptr` on error
    [[nodiscard]]
    const ByteVec* get(ByteSpan key) const noexcept {
        return memtable_.get(key);
    }

    // returns `nullptr` on error
    ByteVec* get(ByteSpan key) noexcept {
        return const_cast<ByteVec*>(std::as_const(memtable_).get(key)); // NOLINT
    }

    // returns `nullptr` on error
    [[nodiscard]]
    std::unique_ptr<SSTable> write_sstable(const std::string& path) const noexcept {
        std::ofstream file(path, std::ios::binary);

        if (!file.is_open()) {
            return nullptr;
        }

        for (const auto& [key, value] : memtable_.data()) {
            auto key_len = static_cast<uint32_t>(key.size());
            file.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
            file.write(reinterpret_cast<const char*>(key.data()), key_len);

            auto val_len = static_cast<uint32_t>(value.size());
            file.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
            file.write(reinterpret_cast<const char*>(value.data()), val_len);
        }

        return std::make_unique<SSTable>(path);
    };
};

// an abstract layer for StorageEngine class with generic types
template <typename K, typename V>
class Database {
    StorageEngine db_;

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
    StorageEngine& get_db() noexcept {
        return db_;
    }
};
