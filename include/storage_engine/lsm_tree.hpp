#pragma once

#include "serialize_deserialize.hpp"

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>

template <typename K, typename V> class Memtable {
    // TODO: change this with to a custom lock free SkipList
    std::map<K, V> data_{};

public:
    template <typename K_, typename V_> void put(K_&& key, V_&& value) noexcept {
        data_.insert_or_assign(std::forward<K_>(key), std::forward<V_>(value));
    }

    const V* get(const K& key) const noexcept {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return nullptr;
        }
        return &(it->second);
    }

    V* get(const K& key) noexcept {
        const auto* const_this = static_cast<const Memtable*>(this);
        return const_cast<V*>(const_this->get(key));
    }

    [[nodiscard]] const std::map<K, V>& data() const noexcept { return data_; }
};

template <typename K, typename V> class SSTable {
    std::string path;

public:
    SSTable(std::string path) : path(std::move(path)) {}

    // returns `nullptr` on error
    std::unique_ptr<V> get(K key) const noexcept {
        struct Pair {
            K key;
            V value;
        };

        std::ifstream file(path, std::ios::binary);

        if (!file.is_open()) {
            return nullptr;
        }

        std::vector<Pair> pairs;

        uint32_t key_length = 0;
        while (file.read(reinterpret_cast<char*>(&key_length), sizeof(key_length))) {
            K iter_key = deserialize_value<K>(file, key_length);

            uint32_t value_length = 0;
            file.read(reinterpret_cast<char*>(&value_length), sizeof(value_length));
            V iter_value = deserialize_value<V>(file, value_length);

            if (file) {
                pairs.emplace_back(iter_key, iter_value);
            } else {
                // TODO: logging maybe?
            }
        }

        file.close();

        for (const auto& pair : pairs) {
            if (pair.key == key) {
                return std::make_unique<V>(pair.value);
            }
        }

        return nullptr;
    }
};

template <typename K, typename V> class DB {
    Memtable<K, V> memtable_{};

public:
    template <typename K_, typename V_> void put(K_&& key, V_&& value) noexcept {
        memtable_.put(std::forward<K_>(key), std::forward<V_>(value));
    }

    const V* get(const K& key) const noexcept { return memtable_.get(key); }
    V* get(const K& key) noexcept { return memtable_.get(key); }

    // returns `nullptr` on error
    std::unique_ptr<SSTable<K, V>> write_sstable(const std::string& path) const noexcept {
        std::ofstream file(path, std::ios::binary);

        if (!file.is_open()) {
            return nullptr;
        }

        for (const auto& [key, value] : memtable_.data()) {
            uint32_t key_len = get_encoded_size<K>(key);
            file.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
            serialize_value<K>(file, key);

            uint32_t val_len = get_encoded_size<V>(value);
            file.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
            serialize_value<V>(file, value);
        }

        file.close();

        return std::make_unique<SSTable<K, V>>(path);
    };
};
