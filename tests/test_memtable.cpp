#include "storage_engine/engine.hpp"
#include "storage_engine/memtable.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

ByteVec to_bytes(std::string_view text) {
    ByteVec bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

struct ParsedRecord {
    RecordType record_type{};
    std::string key;
    std::string value;
};

std::vector<ParsedRecord> parse_records(const std::string& raw) {
    std::vector<ParsedRecord> records;

    const ByteSpan bytes{reinterpret_cast<const std::byte*>(raw.data()), raw.size()};
    size_t pos = 0;

    const auto read_varint = [&](uint32_t& value) {
        const auto decoded = decode_leb128(bytes.subspan(pos));
        REQUIRE(decoded.has_value());
        value = decoded->value;
        pos += decoded->bytes_read;
    };

    while (pos < bytes.size()) {
        const auto record_type = static_cast<RecordType>(std::to_integer<uint8_t>(bytes[pos]));
        pos++;

        uint32_t key_length = 0;
        read_varint(key_length);
        REQUIRE(pos + key_length <= bytes.size());

        std::string key(reinterpret_cast<const char*>(bytes.data() + pos), key_length);
        pos += key_length;

        std::string value;
        if (record_type == RecordType::PUT) {
            uint32_t value_length = 0;
            read_varint(value_length);
            REQUIRE(pos + value_length <= bytes.size());

            value.assign(reinterpret_cast<const char*>(bytes.data() + pos), value_length);
            pos += value_length;
        }

        records.push_back({record_type, std::move(key), std::move(value)});
    }

    return records;
}

const ByteVec* require_memtable_value(const Memtable& table, ByteSpan key) {
    auto result = table.get(key);
    REQUIRE(result.has_value());
    REQUIRE(*result != nullptr);
    return *result;
}

void require_memtable_missing(const Memtable& table, ByteSpan key) {
    auto result = table.get(key);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ErrorCode::NOT_FOUND);
}

void require_memtable_deleted(const Memtable& table, ByteSpan key) {
    auto result = table.get(key);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == ErrorCode::KEY_DELETED);
}

} // namespace

TEST_CASE("Memtable Basic Operations", "[memtable]") {
    if (fs::exists("db.wal")) {
        fs::remove("db.wal");
    }

    Database<std::string, std::string> db{100};

    SECTION("Empty memtable should return nullptr") {
        REQUIRE(db.get("non_existent_key") == nullptr);
    }

    SECTION("Should insert data and retrieve it correctly") {
        db.put("key1", "value1");
        db.put("key2", "value2");

        auto v1 = db.get("key1");
        auto v2 = db.get("key2");

        REQUIRE(v1 != nullptr);
        REQUIRE(*v1 == "value1");
        REQUIRE(v2 != nullptr);
        REQUIRE(*v2 == "value2");
    }

    SECTION("Should overwrite value when putting the same key twice") {
        db.put("key", "old_value");
        db.put("key", "new_value");

        auto v = db.get("key");
        REQUIRE(v != nullptr);
        REQUIRE(*v == "new_value");
    }

    if (fs::exists("db.wal")) {
        fs::remove("db.wal");
    }
}

TEST_CASE("Memtable byte-level operations", "[memtable]") {
    SECTION("Stores and retrieves raw byte entries") {
        Memtable memtable;
        const ByteVec key = to_bytes("key");
        memtable.put(RecordType::PUT, key, to_bytes("value"));

        const Memtable& const_table = memtable;
        REQUIRE(*require_memtable_value(const_table, key) == to_bytes("value"));

        require_memtable_missing(const_table, to_bytes("missing"));
    }

    SECTION("Overwrites the value of an existing key") {
        Memtable memtable;
        const ByteVec key = to_bytes("key");
        memtable.put(RecordType::PUT, key, to_bytes("old"));
        memtable.put(RecordType::PUT, key, to_bytes("new"));

        const Memtable& const_table = memtable;
        REQUIRE(*require_memtable_value(const_table, key) == to_bytes("new"));
    }

    SECTION("Empty values are stored distinctly from missing keys") {
        Memtable memtable;
        const ByteVec key = to_bytes("empty");
        memtable.put(RecordType::PUT, key, ByteVec{});

        const Memtable& const_table = memtable;
        const ByteVec* value = require_memtable_value(const_table, key);
        REQUIRE(value->empty());

        require_memtable_missing(const_table, to_bytes("other"));
    }

    SECTION("Binary keys and values survive a round-trip") {
        Memtable memtable;
        const ByteVec key = to_bytes(std::string_view("\x00\x01\x00", 3));
        const ByteVec value = to_bytes(std::string_view("\xff\x00\xfe", 3));
        memtable.put(RecordType::PUT, key, value);

        const Memtable& const_table = memtable;
        REQUIRE(*require_memtable_value(const_table, key) == value);
    }

    SECTION("Non-const get allows in-place mutation") {
        Memtable memtable;
        const ByteVec key = to_bytes("key");
        memtable.put(RecordType::PUT, key, to_bytes("first"));

        auto found = memtable.get(key);
        REQUIRE(found.has_value());
        REQUIRE(*found != nullptr);

        ByteVec* value = *found;
        *value = to_bytes("second");

        REQUIRE(*require_memtable_value(memtable, key) == to_bytes("second"));
    }

    SECTION("A tombstone hides the previous value") {
        Memtable memtable;
        const ByteVec key = to_bytes("key");
        memtable.put(RecordType::PUT, key, to_bytes("value"));

        memtable.put(RecordType::DELETE, key, ByteVec{});

        const Memtable& const_table = memtable;
        require_memtable_deleted(const_table, key);
    }

    SECTION("Deleting a missing key leaves it missing") {
        Memtable memtable;
        const ByteVec key = to_bytes("missing");
        memtable.put(RecordType::DELETE, key, ByteVec{});

        const Memtable& const_table = memtable;
        require_memtable_deleted(const_table, key);
        require_memtable_missing(const_table, to_bytes("other"));
    }

    SECTION("A PUT after a DELETE revives the key") {
        Memtable memtable;
        const ByteVec key = to_bytes("key");
        memtable.put(RecordType::DELETE, key, ByteVec{});
        memtable.put(RecordType::PUT, key, to_bytes("revived"));

        const Memtable& const_table = memtable;
        REQUIRE(*require_memtable_value(const_table, key) == to_bytes("revived"));
    }

    SECTION("Repeated deletes keep the key tombstoned") {
        Memtable memtable;
        const ByteVec key = to_bytes("key");
        memtable.put(RecordType::PUT, key, to_bytes("value"));
        memtable.put(RecordType::DELETE, key, ByteVec{});
        memtable.put(RecordType::DELETE, key, ByteVec{});

        const Memtable& const_table = memtable;
        require_memtable_deleted(const_table, key);
    }
}

TEST_CASE("Memtable write_to", "[memtable]") {
    SECTION("Writes entries in lexicographic key order with length prefixes") {
        Memtable memtable;
        memtable.put(RecordType::PUT, to_bytes("z"), to_bytes("26"));
        memtable.put(RecordType::PUT, to_bytes("aa"), to_bytes("1"));
        memtable.put(RecordType::PUT, to_bytes("b"), to_bytes("2"));
        memtable.put(RecordType::PUT, to_bytes("c"), to_bytes("3"));

        std::ostringstream stream(std::ios::out | std::ios::binary);
        REQUIRE(memtable.write_to(stream));

        // Keys stay raw in memory, so short keys must not sort before longer
        // ones just because the encoded length prefix is written first.
        const auto records = parse_records(stream.str());
        REQUIRE(records.size() == 4);
        REQUIRE(records[0].record_type == RecordType::PUT);
        REQUIRE(records[0].key == "aa");
        REQUIRE(records[0].value == "1");
        REQUIRE(records[1].record_type == RecordType::PUT);
        REQUIRE(records[1].key == "b");
        REQUIRE(records[1].value == "2");
        REQUIRE(records[2].record_type == RecordType::PUT);
        REQUIRE(records[2].key == "c");
        REQUIRE(records[2].value == "3");
        REQUIRE(records[3].record_type == RecordType::PUT);
        REQUIRE(records[3].key == "z");
        REQUIRE(records[3].value == "26");
    }

    SECTION("Tombstones are written with a DELETE type and no value section") {
        Memtable memtable;
        memtable.put(RecordType::PUT, to_bytes("alive"), to_bytes("value"));
        memtable.put(RecordType::DELETE, to_bytes("dead"), ByteVec{});

        std::ostringstream stream(std::ios::out | std::ios::binary);
        REQUIRE(memtable.write_to(stream));

        const auto records = parse_records(stream.str());
        REQUIRE(records.size() == 2);
        REQUIRE(records[0].record_type == RecordType::PUT);
        REQUIRE(records[0].key == "alive");
        REQUIRE(records[0].value == "value");
        REQUIRE(records[1].record_type == RecordType::DELETE);
        REQUIRE(records[1].key == "dead");
        REQUIRE(records[1].value.empty());
    }

    SECTION("A tombstone does not desynchronize the following record") {
        Memtable memtable;
        // "a" sorts before "b", so the tombstone is written first.
        memtable.put(RecordType::DELETE, to_bytes("a"), ByteVec{});
        memtable.put(RecordType::PUT, to_bytes("b"), to_bytes("2"));

        std::ostringstream stream(std::ios::out | std::ios::binary);
        REQUIRE(memtable.write_to(stream));

        const auto records = parse_records(stream.str());
        REQUIRE(records.size() == 2);
        REQUIRE(records[0].record_type == RecordType::DELETE);
        REQUIRE(records[0].key == "a");
        REQUIRE(records[1].record_type == RecordType::PUT);
        REQUIRE(records[1].key == "b");
        REQUIRE(records[1].value == "2");
    }

    SECTION("The value passed with a DELETE is not persisted") {
        Memtable memtable;
        memtable.put(RecordType::DELETE, to_bytes("a"), to_bytes("ignored"));
        memtable.put(RecordType::PUT, to_bytes("b"), to_bytes("2"));

        std::ostringstream stream(std::ios::out | std::ios::binary);
        REQUIRE(memtable.write_to(stream));

        const auto records = parse_records(stream.str());
        REQUIRE(records.size() == 2);
        REQUIRE(records[0].record_type == RecordType::DELETE);
        REQUIRE(records[0].key == "a");
        REQUIRE(records[0].value.empty());
        REQUIRE(records[1].record_type == RecordType::PUT);
        REQUIRE(records[1].key == "b");
        REQUIRE(records[1].value == "2");
    }

    SECTION("Empty memtable writes nothing") {
        Memtable memtable;

        std::ostringstream stream(std::ios::out | std::ios::binary);
        REQUIRE(memtable.write_to(stream));
        REQUIRE(stream.str().empty());
    }

    SECTION("Returns false when the stream fails") {
        Memtable memtable;
        memtable.put(RecordType::PUT, to_bytes("key"), to_bytes("value"));

        struct FailingBuffer : std::streambuf {
        protected:
            std::streamsize xsputn(const char* /*__s*/, std::streamsize /*__n*/) override {
                return 0;
            }
            int overflow(int /*__c*/) override { return traits_type::eof(); }
        } buffer;
        std::ostream failing_stream(&buffer);

        REQUIRE_FALSE(memtable.write_to(failing_stream));
    }
}

TEST_CASE("Memtable clear", "[memtable]") {
    SECTION("Removes all entries") {
        Memtable memtable;
        memtable.put(RecordType::PUT, to_bytes("a"), to_bytes("1"));
        memtable.put(RecordType::PUT, to_bytes("b"), to_bytes("2"));

        memtable.clear();

        require_memtable_missing(memtable, to_bytes("a"));
        require_memtable_missing(memtable, to_bytes("b"));
    }

    SECTION("Cleared memtable writes nothing") {
        Memtable memtable;
        memtable.put(RecordType::PUT, to_bytes("a"), to_bytes("1"));

        memtable.clear();

        std::ostringstream stream(std::ios::out | std::ios::binary);
        REQUIRE(memtable.write_to(stream));
        REQUIRE(stream.str().empty());
    }
}
