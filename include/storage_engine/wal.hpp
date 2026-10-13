#pragma once

#include "storage_engine/errors.hpp"
#include "storage_engine/memtable.hpp"
#include "storage_engine/serialize_deserialize.hpp"
#include "storage_engine/types.hpp"

#include <expected>
#include <filesystem>
#include <fstream>

struct WalReplayResult {
    Memtable memtable;
    size_t record_count{0};
};

// wal is for memtable recovery, when a memtable flush occurs wal should be cleared
class Wal {
    std::ofstream file_;

public:
    Wal(const std::string& path) : file_(path, std::ios::out | std::ios::app) {
        if (!file_.is_open()) {
            std::println(stderr, "Failed to open file: {}", path);
        }
    }

    bool write(const ByteVec& key, const ByteVec& value) noexcept {
        auto encoded_key = encode<ByteVec>(key);
        if (!encoded_key.has_value()) {
            return false;
        }

        auto encoded_value = encode<ByteVec>(value);
        if (!encoded_value.has_value()) {
            return false;
        }

        file_.write(reinterpret_cast<const char*>(encoded_key->data()),
                    static_cast<std::streamsize>(encoded_key->size()));
        file_.write(reinterpret_cast<const char*>(encoded_value->data()),
                    static_cast<std::streamsize>(encoded_value->size()));
        file_.flush();

        return static_cast<bool>(file_);
    }

    static std::expected<WalReplayResult, ErrorCode> replay_wal(const std::string& path) noexcept;
    void clear(const std::string& path) noexcept {
        file_.close();

        // empty the file
        std::error_code ec;
        std::filesystem::resize_file(path, 0, ec);

        file_ = std::ofstream{path, std::ios::out | std::ios::app};
        if (!file_.is_open()) {
            std::println(stderr, "Failed to open file: {}", path);
        }
    }
};
