#pragma once

#include "storage_engine/errors.hpp"
#include "storage_engine/memtable.hpp"
#include "storage_engine/serialize_deserialize.hpp"
#include "storage_engine/types.hpp"

#include <expected>
#include <filesystem>
#include <fstream>
#include <system_error>

struct WalReplayResult {
    Memtable memtable;
    size_t record_count{};
    size_t bytes_consumed{};
    bool had_torn_tail{false};
};

// wal is for memtable recovery, when a memtable flush occurs wal should be cleared
class Wal {
    std::ofstream file_;
    std::string path_;

public:
    Wal(const std::string& path) : file_(path, std::ios::out | std::ios::app), path_(path) {
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

    void truncate_to(size_t truncate_from) noexcept {
        file_.close();

        std::error_code ec;
        std::filesystem::resize_file(path_, truncate_from, ec);

        file_ = std::ofstream{path_, std::ios::out | std::ios::app};
        if (!file_.is_open()) {
            std::println(stderr, "Failed to open file: {}", path_);
        }
    }

    void clear() noexcept { truncate_to(0); }
};
