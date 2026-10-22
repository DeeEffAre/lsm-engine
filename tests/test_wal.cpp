#include "storage_engine/serialize_deserialize.hpp"
#include "storage_engine/wal.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
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

ByteVec read_file_bytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return to_bytes(buffer.str());
}

constexpr std::byte byte_of(RecordType type) noexcept {
    return static_cast<std::byte>(static_cast<uint8_t>(type));
}

std::string memtable_payload(const Memtable& memtable) {
    std::ostringstream stream(std::ios::out | std::ios::binary);
    REQUIRE(memtable.write_to(stream));
    return stream.str();
}

const ByteVec* require_memtable_value(const Memtable& memtable, ByteSpan key) {
    auto result = memtable.get(key);
    REQUIRE(result.has_value());
    REQUIRE(*result != nullptr);
    return *result;
}

void require_memtable_missing(const Memtable& memtable, ByteSpan key) {
    auto result = memtable.get(key);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ErrorCode::NOT_FOUND);
}

void require_memtable_deleted(const Memtable& memtable, ByteSpan key) {
    auto result = memtable.get(key);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ErrorCode::KEY_DELETED);
}

} // namespace

TEST_CASE("Wal write and replay round-trip", "[wal]") {
    remove_if_exists(WAL_PATH);

    SECTION("Single entry") {
        Wal wal(std::string{WAL_PATH});
        REQUIRE(wal.write_put(to_bytes("key"), to_bytes("value")));

        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 1);

        const ByteVec* value = require_memtable_value(replay->memtable, to_bytes("key"));
        REQUIRE(*value == to_bytes("value"));
        require_memtable_missing(replay->memtable, to_bytes("missing"));
    }

    SECTION("Multiple entries") {
        Wal wal(std::string{WAL_PATH});
        REQUIRE(wal.write_put(to_bytes("a"), to_bytes("1")));
        REQUIRE(wal.write_put(to_bytes("b"), to_bytes("2")));
        REQUIRE(wal.write_put(to_bytes("c"), to_bytes("3")));

        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 3);

        for (const auto& [key, value] :
             std::initializer_list<std::pair<std::string_view, std::string_view>>{
                 {"a", "1"}, {"b", "2"}, {"c", "3"}}) {
            const ByteVec* stored = require_memtable_value(replay->memtable, to_bytes(key));
            REQUIRE(*stored == to_bytes(value));
        }
    }

    SECTION("Overwrites the previous value of the same key") {
        Wal wal(std::string{WAL_PATH});
        REQUIRE(wal.write_put(to_bytes("key"), to_bytes("first")));
        REQUIRE(wal.write_put(to_bytes("key"), to_bytes("second")));

        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 2); // two log records for the same key

        const ByteVec* value = require_memtable_value(replay->memtable, to_bytes("key"));
        REQUIRE(*value == to_bytes("second"));
    }

    SECTION("Empty value") {
        Wal wal(std::string{WAL_PATH});
        REQUIRE(wal.write_put(to_bytes("key"), ByteVec{}));

        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());

        const ByteVec* value = require_memtable_value(replay->memtable, to_bytes("key"));
        REQUIRE(value->empty());
    }

    SECTION("Empty key") {
        Wal wal(std::string{WAL_PATH});
        REQUIRE(wal.write_put(ByteVec{}, to_bytes("value")));

        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());

        const ByteVec* value = require_memtable_value(replay->memtable, ByteVec{});
        REQUIRE(*value == to_bytes("value"));
    }

    SECTION("Binary keys and values with null bytes") {
        const ByteVec key{std::byte{0x00}, std::byte{0x80}, std::byte{0xFF}, std::byte{0x00}};
        const ByteVec value{std::byte{0xFF}, std::byte{0x00}, std::byte{0x80}, std::byte{0x7F}};

        Wal wal(std::string{WAL_PATH});
        REQUIRE(wal.write_put(key, value));

        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());

        const ByteVec* stored = require_memtable_value(replay->memtable, key);
        REQUIRE(*stored == value);
    }

    SECTION("Large key and value need multi-byte LEB128 lengths") {
        const std::string big_key(200, 'k');
        const std::string big_value(300, 'v');

        Wal wal(std::string{WAL_PATH});
        REQUIRE(wal.write_put(*serialize_value<std::string>(big_key),
                              *serialize_value<std::string>(big_value)));

        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());

        const ByteVec* stored = require_memtable_value(replay->memtable, to_bytes(big_key));
        REQUIRE(*stored == to_bytes(big_value));
    }

    remove_if_exists(WAL_PATH);
}

TEST_CASE("Wal writes and replays delete records", "[wal][tombstone]") {
    remove_if_exists(WAL_PATH);

    SECTION("A delete record is written without a value section") {
        {
            Wal wal(std::string{WAL_PATH});
            REQUIRE(wal.write_delete(to_bytes("key")));
        }

        const ByteVec expected{byte_of(RecordType::DELETE), std::byte{0x03}, std::byte{'k'},
                               std::byte{'e'}, std::byte{'y'}};
        REQUIRE(read_file_bytes(std::string{WAL_PATH}) == expected);

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 1);
        REQUIRE_FALSE(replay->had_torn_tail);
        REQUIRE(replay->bytes_consumed == expected.size());
        require_memtable_deleted(replay->memtable, to_bytes("key"));
        REQUIRE(to_bytes(memtable_payload(replay->memtable)) == expected);
    }

    SECTION("A delete overwrites an earlier PUT for the same key") {
        {
            Wal wal(std::string{WAL_PATH});
            REQUIRE(wal.write_put(to_bytes("key"), to_bytes("old")));
            REQUIRE(wal.write_delete(to_bytes("key")));
        }

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 2);
        require_memtable_deleted(replay->memtable, to_bytes("key"));

        // The memtable keeps only the latest record: the tombstone.
        const ByteVec expected{byte_of(RecordType::DELETE), std::byte{0x03}, std::byte{'k'},
                               std::byte{'e'}, std::byte{'y'}};
        REQUIRE(to_bytes(memtable_payload(replay->memtable)) == expected);
    }

    SECTION("A PUT after a DELETE revives the key") {
        {
            Wal wal(std::string{WAL_PATH});
            REQUIRE(wal.write_put(to_bytes("key"), to_bytes("old")));
            REQUIRE(wal.write_delete(to_bytes("key")));
            REQUIRE(wal.write_put(to_bytes("key"), to_bytes("new")));
        }

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 3);

        const ByteVec* value = require_memtable_value(replay->memtable, to_bytes("key"));
        REQUIRE(*value == to_bytes("new"));

        const ByteVec expected{byte_of(RecordType::PUT), std::byte{0x03}, std::byte{'k'},
                               std::byte{'e'},           std::byte{'y'},  std::byte{0x03},
                               std::byte{'n'},           std::byte{'e'},  std::byte{'w'}};
        REQUIRE(to_bytes(memtable_payload(replay->memtable)) == expected);
    }

    SECTION("A delete does not swallow the following record") {
        {
            Wal wal(std::string{WAL_PATH});
            REQUIRE(wal.write_delete(to_bytes("a")));
            REQUIRE(wal.write_put(to_bytes("b"), to_bytes("2")));
        }

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 2);
        require_memtable_deleted(replay->memtable, to_bytes("a"));

        const ByteVec* b = require_memtable_value(replay->memtable, to_bytes("b"));
        REQUIRE(*b == to_bytes("2"));

        const ByteVec expected{byte_of(RecordType::DELETE),
                               std::byte{0x01},
                               std::byte{'a'},
                               byte_of(RecordType::PUT),
                               std::byte{0x01},
                               std::byte{'b'},
                               std::byte{0x01},
                               std::byte{'2'}};
        REQUIRE(to_bytes(memtable_payload(replay->memtable)) == expected);
    }

    SECTION("A delete is durable while the writer is still open") {
        Wal wal(std::string{WAL_PATH});
        REQUIRE(wal.write_delete(to_bytes("key")));

        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 1);
        REQUIRE_FALSE(replay->had_torn_tail);
        require_memtable_deleted(replay->memtable, to_bytes("key"));
    }

    remove_if_exists(WAL_PATH);
}

TEST_CASE("Wal appends to an existing file", "[wal]") {
    remove_if_exists(WAL_PATH);

    {
        Wal wal(std::string{WAL_PATH});
        REQUIRE(wal.write_put(to_bytes("a"), to_bytes("1")));
    }
    {
        Wal wal(std::string{WAL_PATH});
        REQUIRE(wal.write_put(to_bytes("b"), to_bytes("2")));
    }

    Wal wal(std::string{WAL_PATH});
    auto replay = wal.replay_wal();
    REQUIRE(replay.has_value());
    REQUIRE(replay->record_count == 2);

    const ByteVec* a = require_memtable_value(replay->memtable, to_bytes("a"));
    const ByteVec* b = require_memtable_value(replay->memtable, to_bytes("b"));
    REQUIRE(*a == to_bytes("1"));
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
    REQUIRE(wal.write_put(to_bytes("key"), to_bytes("value")));

    // A write-ahead log must be readable right after write() returns, otherwise
    // a crash would lose acknowledged writes.
    auto replay = wal.replay_wal();
    REQUIRE(replay.has_value());

    const ByteVec* value = require_memtable_value(replay->memtable, to_bytes("key"));
    REQUIRE(*value == to_bytes("value"));

    remove_if_exists(WAL_PATH);
}

TEST_CASE("Wal replay error handling", "[wal]") {
    remove_if_exists(WAL_PATH);

    SECTION("Missing file returns FILE_ERROR") {
        // The constructor creates the file, so remove it afterwards to simulate
        // a database whose WAL path disappeared.
        Wal wal(std::string{WAL_PATH});
        remove_if_exists(WAL_PATH);

        auto replay = wal.replay_wal();
        REQUIRE_FALSE(replay.has_value());
        REQUIRE(replay.error() == ErrorCode::FILE_ERROR);
    }

    SECTION("Empty file yields an empty memtable") {
        write_raw_file(std::string{WAL_PATH}, ByteVec{});

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->record_count == 0);
        REQUIRE_FALSE(replay->had_torn_tail);
        REQUIRE(replay->bytes_consumed == 0);
        require_memtable_missing(replay->memtable, to_bytes("anything"));
    }

    SECTION("Truncated value payload is treated as a torn tail") {
        // record type = PUT, key length = 1, key = 'a',
        // value length = 5, payload = "he" (truncated)
        write_raw_file(std::string{WAL_PATH},
                       ByteVec{byte_of(RecordType::PUT), std::byte{0x01}, std::byte{'a'},
                               std::byte{0x05}, std::byte{'h'}, std::byte{'e'}});

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->had_torn_tail);
        REQUIRE(replay->record_count == 0);
        require_memtable_missing(replay->memtable, to_bytes("a"));
    }

    SECTION("Truncated varint is treated as a torn tail") {
        // record type = PUT, key length = 1, key = 'a',
        // then a varint with the continuation bit set and EOF
        write_raw_file(std::string{WAL_PATH}, ByteVec{byte_of(RecordType::PUT), std::byte{0x01},
                                                      std::byte{'a'}, std::byte{0x80}});

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->had_torn_tail);
        REQUIRE(replay->record_count == 0);
    }

    SECTION("Varint value that overflows uint32_t returns OVERFLOW") {
        write_raw_file(std::string{WAL_PATH},
                       ByteVec{byte_of(RecordType::PUT), std::byte{0xFF}, std::byte{0xFF},
                               std::byte{0xFF}, std::byte{0xFF}, std::byte{0x7F}});

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE_FALSE(replay.has_value());
        REQUIRE(replay.error() == ErrorCode::OVERFLOW);
    }

    SECTION("Varint longer than 5 bytes returns TOO_MANY_BYTES") {
        write_raw_file(std::string{WAL_PATH},
                       ByteVec{byte_of(RecordType::PUT), std::byte{0xFF}, std::byte{0xFF},
                               std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}});

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE_FALSE(replay.has_value());
        REQUIRE(replay.error() == ErrorCode::TOO_MANY_BYTES);
    }

    remove_if_exists(WAL_PATH);
}

TEST_CASE("Wal replay tolerates a torn tail", "[wal]") {
    remove_if_exists(WAL_PATH);

    // key "a" -> "1" and key "b" -> "2" as encoded records
    const ByteVec record_a_put{byte_of(RecordType::PUT), std::byte{0x01}, std::byte{'a'},
                               std::byte{0x01}, std::byte{'1'}};
    const ByteVec record_b_put{byte_of(RecordType::PUT), std::byte{0x01}, std::byte{'b'},
                               std::byte{0x01}, std::byte{'2'}};
    const ByteVec record_c_delete{byte_of(RecordType::DELETE), std::byte{0x01}, std::byte{'c'}};

    auto valid_prefix = [&] {
        ByteVec bytes = record_a_put;
        bytes.insert(bytes.end(), record_b_put.begin(), record_b_put.end());
        return bytes;
    };

    SECTION("Clean file reports no torn tail") {
        const ByteVec bytes = valid_prefix();
        write_raw_file(std::string{WAL_PATH}, bytes);

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE_FALSE(replay->had_torn_tail);
        REQUIRE(replay->record_count == 2);
        REQUIRE(replay->bytes_consumed == bytes.size());
    }

    SECTION("Torn varint at the tail keeps the complete records") {
        ByteVec bytes = valid_prefix();
        bytes.push_back(byte_of(RecordType::PUT));
        bytes.push_back(std::byte{0x80}); // continuation bit set, then EOF
        write_raw_file(std::string{WAL_PATH}, bytes);

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->had_torn_tail);
        REQUIRE(replay->record_count == 2);
        REQUIRE(replay->bytes_consumed == record_a_put.size() + record_b_put.size());

        const ByteVec* a = require_memtable_value(replay->memtable, to_bytes("a"));
        REQUIRE(*a == to_bytes("1"));
        const ByteVec* b = require_memtable_value(replay->memtable, to_bytes("b"));
        REQUIRE(*b == to_bytes("2"));
    }

    SECTION("A record type alone at the tail is a torn tail") {
        ByteVec bytes = valid_prefix();
        bytes.push_back(byte_of(RecordType::DELETE));
        write_raw_file(std::string{WAL_PATH}, bytes);

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->had_torn_tail);
        REQUIRE(replay->record_count == 2);
        REQUIRE(replay->bytes_consumed == record_a_put.size() + record_b_put.size());
    }

    SECTION("Torn key payload at the tail keeps the complete records") {
        ByteVec bytes = record_a_put;
        bytes.push_back(byte_of(RecordType::PUT));
        bytes.push_back(std::byte{0x05}); // key length = 5...
        bytes.push_back(std::byte{'x'});  // ...but only one key byte exists
        write_raw_file(std::string{WAL_PATH}, bytes);

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->had_torn_tail);
        REQUIRE(replay->record_count == 1);
        REQUIRE(replay->bytes_consumed == record_a_put.size());
        require_memtable_missing(replay->memtable, to_bytes("x"));
    }

    SECTION("Record cut after the key keeps the complete records") {
        ByteVec bytes = record_a_put;
        bytes.push_back(byte_of(RecordType::PUT));
        bytes.push_back(std::byte{0x01}); // key length = 1
        bytes.push_back(std::byte{'z'});  // key "z", then EOF (no value length)
        write_raw_file(std::string{WAL_PATH}, bytes);

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->had_torn_tail);
        REQUIRE(replay->record_count == 1);
        REQUIRE(replay->bytes_consumed == record_a_put.size());
        require_memtable_missing(replay->memtable, to_bytes("z"));
    }

    SECTION("Torn value payload at the tail keeps the complete records") {
        ByteVec bytes = record_a_put;
        bytes.push_back(byte_of(RecordType::PUT));
        bytes.push_back(std::byte{0x01}); // key length = 1
        bytes.push_back(std::byte{'z'});  // key "z"
        bytes.push_back(std::byte{0x03}); // value length = 3...
        bytes.push_back(std::byte{'v'});  // ...but only one value byte exists
        write_raw_file(std::string{WAL_PATH}, bytes);

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->had_torn_tail);
        REQUIRE(replay->record_count == 1);
        REQUIRE(replay->bytes_consumed == record_a_put.size());
        require_memtable_missing(replay->memtable, to_bytes("z"));
    }

    SECTION("Torn DELETE key payload at the tail keeps the complete records") {
        ByteVec bytes = record_a_put;
        bytes.push_back(byte_of(RecordType::DELETE));
        bytes.push_back(std::byte{0x05}); // key length = 5...
        bytes.push_back(std::byte{'x'});  // ...but only one key byte exists
        write_raw_file(std::string{WAL_PATH}, bytes);

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE(replay->had_torn_tail);
        REQUIRE(replay->record_count == 1);
        REQUIRE(replay->bytes_consumed == record_a_put.size());
        require_memtable_missing(replay->memtable, to_bytes("x"));
    }

    SECTION("A complete DELETE at the tail is not torn") {
        ByteVec bytes = valid_prefix();
        bytes.insert(bytes.end(), record_c_delete.begin(), record_c_delete.end());
        write_raw_file(std::string{WAL_PATH}, bytes);

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE(replay.has_value());
        REQUIRE_FALSE(replay->had_torn_tail);
        REQUIRE(replay->record_count == 3);
        REQUIRE(replay->bytes_consumed == bytes.size());
        require_memtable_deleted(replay->memtable, to_bytes("c"));
    }

    SECTION("Corrupted varint after valid records is an error") {
        ByteVec bytes = record_a_put;
        bytes.insert(bytes.end(), {byte_of(RecordType::PUT), std::byte{0xFF}, std::byte{0xFF},
                                   std::byte{0xFF}, std::byte{0xFF}, std::byte{0x7F}});
        write_raw_file(std::string{WAL_PATH}, bytes);

        Wal wal(std::string{WAL_PATH});
        auto replay = wal.replay_wal();
        REQUIRE_FALSE(replay.has_value());
        REQUIRE(replay.error() == ErrorCode::OVERFLOW);
    }

    remove_if_exists(WAL_PATH);
}
