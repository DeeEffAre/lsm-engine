#include "storage_engine/serialize_deserialize.hpp"
#include "storage_engine/wal.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

constexpr std::string_view WAL_PATH = "test_wal_data.wal";

ByteVec to_bytes(std::string_view text) {
    ByteVec bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

void remove_if_exists(std::string_view path) {
    if (fs::exists(path)) {
        fs::remove(path);
    }
}

void write_raw_file(const std::string& path, const ByteVec& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("Wal write and replay round-trip", "[wal]") {
    remove_if_exists(WAL_PATH);

    SECTION("Single entry") {
        {
            Wal wal(std::string{WAL_PATH});
            wal.write(to_bytes("key"), to_bytes("value"));
        }

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 1);

        const ByteVec* value = replay->memtable.get(to_bytes("key"));
        REQUIRE(value != nullptr);
        REQUIRE(*value == to_bytes("value"));
        REQUIRE(replay->memtable.get(to_bytes("missing")) == nullptr);
    }

    SECTION("Multiple entries") {
        {
            Wal wal(std::string{WAL_PATH});
            wal.write(to_bytes("a"), to_bytes("1"));
            wal.write(to_bytes("b"), to_bytes("2"));
            wal.write(to_bytes("c"), to_bytes("3"));
        }

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 3);

        for (const auto& [key, value] :
             std::initializer_list<std::pair<std::string_view, std::string_view>>{
                 {"a", "1"}, {"b", "2"}, {"c", "3"}}) {
            const ByteVec* stored = replay->memtable.get(to_bytes(key));
            REQUIRE(stored != nullptr);
            REQUIRE(*stored == to_bytes(value));
        }
    }

    SECTION("Overwrites the previous value of the same key") {
        {
            Wal wal(std::string{WAL_PATH});
            wal.write(to_bytes("key"), to_bytes("first"));
            wal.write(to_bytes("key"), to_bytes("second"));
        }

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 2); // two log records for the same key

        const ByteVec* value = replay->memtable.get(to_bytes("key"));
        REQUIRE(value != nullptr);
        REQUIRE(*value == to_bytes("second"));
    }

    SECTION("Empty value") {
        {
            Wal wal(std::string{WAL_PATH});
            const auto empty_byte_vec = ByteVec{};
            wal.write(to_bytes("key"), ByteVec{});
        }

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE(replay.has_value());

        const ByteVec* value = replay->memtable.get(to_bytes("key"));
        REQUIRE(value != nullptr);
        REQUIRE(value->empty());
    }

    SECTION("Empty key") {
        {
            Wal wal(std::string{WAL_PATH});
            wal.write(ByteVec{}, to_bytes("value"));
        }

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE(replay.has_value());

        const ByteVec* value = replay->memtable.get(ByteVec{});
        REQUIRE(value != nullptr);
        REQUIRE(*value == to_bytes("value"));
    }

    SECTION("Binary keys and values with null bytes") {
        const ByteVec key{std::byte{0x00}, std::byte{0x80}, std::byte{0xFF}, std::byte{0x00}};
        const ByteVec value{std::byte{0xFF}, std::byte{0x00}, std::byte{0x80}, std::byte{0x7F}};

        {
            Wal wal(std::string{WAL_PATH});
            wal.write(key, value);
        }

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE(replay.has_value());

        const ByteVec* stored = replay->memtable.get(key);
        REQUIRE(stored != nullptr);
        REQUIRE(*stored == value);
    }

    SECTION("Large key and value need multi-byte LEB128 lengths") {
        const std::string big_key(200, 'k');
        const std::string big_value(300, 'v');

        {
            Wal wal(std::string{WAL_PATH});
            wal.write(*serialize_value<std::string>(big_key),
                      *serialize_value<std::string>(big_value));
        }

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE(replay.has_value());

        const ByteVec* stored = replay->memtable.get(to_bytes(big_key));
        REQUIRE(stored != nullptr);
        REQUIRE(*stored == to_bytes(big_value));
    }

    remove_if_exists(WAL_PATH);
}

TEST_CASE("Wal appends to an existing file", "[wal]") {
    remove_if_exists(WAL_PATH);

    {
        Wal wal(std::string{WAL_PATH});
        wal.write(to_bytes("a"), to_bytes("1"));
    }
    {
        Wal wal(std::string{WAL_PATH});
        wal.write(to_bytes("b"), to_bytes("2"));
    }

    auto replay = Wal::replay_wal(std::string{WAL_PATH});
    REQUIRE(replay.has_value());
    REQUIRE(replay->record_count == 2);

    const ByteVec* a = replay->memtable.get(to_bytes("a"));
    const ByteVec* b = replay->memtable.get(to_bytes("b"));
    REQUIRE(a != nullptr);
    REQUIRE(*a == to_bytes("1"));
    REQUIRE(b != nullptr);
    REQUIRE(*b == to_bytes("2"));

    remove_if_exists(WAL_PATH);
}

TEST_CASE("Wal creates the file on construction", "[wal]") {
    remove_if_exists(WAL_PATH);

    {
        Wal wal(std::string{WAL_PATH});
    }

    REQUIRE(fs::exists(WAL_PATH));

    remove_if_exists(WAL_PATH);
}

TEST_CASE("Wal writes are durable while the writer is still open", "[wal]") {
    remove_if_exists(WAL_PATH);

    Wal wal{std::string{WAL_PATH}};
    wal.write(to_bytes("key"), to_bytes("value"));

    // A write-ahead log must be readable right after write() returns, otherwise
    // a crash would lose acknowledged writes.
    auto replay = Wal::replay_wal(std::string{WAL_PATH});
    REQUIRE(replay.has_value());

    const ByteVec* value = replay->memtable.get(to_bytes("key"));
    REQUIRE(value != nullptr);
    REQUIRE(*value == to_bytes("value"));

    remove_if_exists(WAL_PATH);
}

TEST_CASE("Wal replay error handling", "[wal]") {
    remove_if_exists(WAL_PATH);

    SECTION("Missing file returns FILE_ERROR") {
        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE_FALSE(replay.has_value());
        REQUIRE(replay.error() == ErrorCode::FILE_ERROR);
    }

    SECTION("Empty file yields an empty memtable") {
        write_raw_file(std::string{WAL_PATH}, ByteVec{});

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE(replay.has_value());
        REQUIRE(replay->memtable.get(to_bytes("anything")) == nullptr);
    }

    SECTION("Truncated value payload returns NOT_ENOUGH_BYTES_LEFT") {
        // key length = 1, key = 'a', value length = 5, payload = "he" (truncated)
        write_raw_file(std::string{WAL_PATH},
                       ByteVec{std::byte{0x01}, std::byte{'a'}, std::byte{0x05}, std::byte{'h'},
                               std::byte{'e'}});

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE_FALSE(replay.has_value());
        REQUIRE(replay.error() == ErrorCode::NOT_ENOUGH_BYTES_LEFT);
    }

    SECTION("Truncated varint returns UNEXPECTED_END") {
        // key length = 1, key = 'a', then a varint with the continuation bit set and EOF
        write_raw_file(std::string{WAL_PATH},
                       ByteVec{std::byte{0x01}, std::byte{'a'}, std::byte{0x80}});

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE_FALSE(replay.has_value());
        REQUIRE(replay.error() == ErrorCode::UNEXPECTED_END);
    }

    SECTION("Varint value that overflows uint32_t returns OVERFLOW") {
        write_raw_file(std::string{WAL_PATH},
                       ByteVec{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                               std::byte{0x7F}});

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE_FALSE(replay.has_value());
        REQUIRE(replay.error() == ErrorCode::OVERFLOW);
    }

    SECTION("Varint longer than 5 bytes returns TOO_MANY_BYTES") {
        write_raw_file(std::string{WAL_PATH},
                       ByteVec{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                               std::byte{0xFF}});

        auto replay = Wal::replay_wal(std::string{WAL_PATH});
        REQUIRE_FALSE(replay.has_value());
        REQUIRE(replay.error() == ErrorCode::TOO_MANY_BYTES);
    }

    remove_if_exists(WAL_PATH);
}
