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

std::vector<std::pair<std::string, std::string>> parse_entries(const std::string& raw) { // NOLINT
    std::vector<std::pair<std::string, std::string>> entries;

    const ByteSpan bytes{reinterpret_cast<const std::byte*>(raw.data()), raw.size()};
    ByteReader reader{bytes};

    while (true) {
        auto key_length = reader.read_varint();
        if (!key_length) {
            REQUIRE(key_length.error() == ErrorCode::END_OF_STREAM);
            break;
        }

        auto key = reader.read_bytes(*key_length);
        REQUIRE(key.has_value());

        auto value_length = reader.read_varint();
        REQUIRE(value_length.has_value());

        auto value = reader.read_bytes(*value_length);
        REQUIRE(value.has_value());

        entries.emplace_back(
            std::string(reinterpret_cast<const char*>(key->data()), key->size()),
            std::string(reinterpret_cast<const char*>(value->data()), value->size()));
    }

    return entries;
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
        memtable.put(key, to_bytes("value"));

        const Memtable& const_table = memtable;
        const ByteVec* value = const_table.get(key);
        REQUIRE(value != nullptr);
        REQUIRE(*value == to_bytes("value"));

        REQUIRE(const_table.get(to_bytes("missing")) == nullptr);
    }

    SECTION("Overwrites the value of an existing key") {
        Memtable memtable;
        const ByteVec key = to_bytes("key");
        memtable.put(key, to_bytes("old"));
        memtable.put(key, to_bytes("new"));

        const Memtable& const_table = memtable;
        const ByteVec* value = const_table.get(key);
        REQUIRE(value != nullptr);
        REQUIRE(*value == to_bytes("new"));
    }

    SECTION("Empty values are stored distinctly from missing keys") {
        Memtable memtable;
        const ByteVec key = to_bytes("empty");
        memtable.put(key, ByteVec{});

        const Memtable& const_table = memtable;
        const ByteVec* value = const_table.get(key);
        REQUIRE(value != nullptr);
        REQUIRE(value->empty());

        REQUIRE(const_table.get(to_bytes("other")) == nullptr);
    }

    SECTION("Binary keys and values survive a round-trip") {
        Memtable memtable;
        const ByteVec key = to_bytes(std::string_view("\x00\x01\x00", 3));
        const ByteVec value = to_bytes(std::string_view("\xff\x00\xfe", 3));
        memtable.put(key, value);

        const Memtable& const_table = memtable;
        const ByteVec* found = const_table.get(key);
        REQUIRE(found != nullptr);
        REQUIRE(*found == value);
    }

    SECTION("Non-const get allows in-place mutation") {
        Memtable memtable;
        const ByteVec key = to_bytes("key");
        memtable.put(key, to_bytes("first"));

        ByteVec* value = memtable.get(key);
        REQUIRE(value != nullptr);
        *value = to_bytes("second");

        REQUIRE(*memtable.get(key) == to_bytes("second"));
    }
}

TEST_CASE("Memtable write_to", "[memtable]") {
    SECTION("Writes entries in lexicographic key order with length prefixes") {
        Memtable memtable;
        memtable.put(to_bytes("z"), to_bytes("26"));
        memtable.put(to_bytes("aa"), to_bytes("1"));
        memtable.put(to_bytes("b"), to_bytes("2"));
        memtable.put(to_bytes("c"), to_bytes("3"));

        std::ostringstream stream(std::ios::out | std::ios::binary);
        REQUIRE(memtable.write_to(stream));

        // Keys stay raw in memory, so short keys must not sort before longer
        // ones just because the encoded length prefix is written first.
        const auto entries = parse_entries(stream.str());
        REQUIRE(entries.size() == 4);
        REQUIRE(entries[0].first == "aa");
        REQUIRE(entries[0].second == "1");
        REQUIRE(entries[1].first == "b");
        REQUIRE(entries[1].second == "2");
        REQUIRE(entries[2].first == "c");
        REQUIRE(entries[2].second == "3");
        REQUIRE(entries[3].first == "z");
        REQUIRE(entries[3].second == "26");
    }

    SECTION("Empty memtable writes nothing") {
        Memtable memtable;

        std::ostringstream stream(std::ios::out | std::ios::binary);
        REQUIRE(memtable.write_to(stream));
        REQUIRE(stream.str().empty());
    }

    SECTION("Returns false when the stream fails") {
        Memtable memtable;
        memtable.put(to_bytes("key"), to_bytes("value"));

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
        memtable.put(to_bytes("a"), to_bytes("1"));
        memtable.put(to_bytes("b"), to_bytes("2"));

        memtable.clear();

        REQUIRE(memtable.get(to_bytes("a")) == nullptr);
        REQUIRE(memtable.get(to_bytes("b")) == nullptr);
    }

    SECTION("Cleared memtable writes nothing") {
        Memtable memtable;
        memtable.put(to_bytes("a"), to_bytes("1"));

        memtable.clear();

        std::ostringstream stream(std::ios::out | std::ios::binary);
        REQUIRE(memtable.write_to(stream));
        REQUIRE(stream.str().empty());
    }
}
