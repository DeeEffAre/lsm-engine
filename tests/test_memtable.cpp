#include "storage_engine/engine.hpp"
#include "storage_engine/memtable.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

ByteVec to_bytes(std::string_view text) {
    ByteVec bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

std::vector<std::pair<std::string, std::string>> parse_entries(const std::string& raw) {
    std::vector<std::pair<std::string, std::string>> entries;

    std::size_t offset = 0;
    while (offset < raw.size()) {
        uint32_t key_len = 0;
        std::memcpy(&key_len, raw.data() + offset, sizeof(key_len));
        offset += sizeof(key_len);

        std::string key(raw.data() + offset, key_len);
        offset += key_len;

        uint32_t value_len = 0;
        std::memcpy(&value_len, raw.data() + offset, sizeof(value_len));
        offset += sizeof(value_len);

        std::string value(raw.data() + offset, value_len);
        offset += value_len;

        entries.emplace_back(std::move(key), std::move(value));
    }

    return entries;
}

} // namespace

TEST_CASE("Memtable Basic Operations", "[memtable]") {
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
        memtable.put(to_bytes("b"), to_bytes("2"));
        memtable.put(to_bytes("a"), to_bytes("1"));
        memtable.put(to_bytes("c"), to_bytes("3"));

        std::ostringstream stream(std::ios::out | std::ios::binary);
        REQUIRE(memtable.write_to(stream));

        const auto entries = parse_entries(stream.str());
        REQUIRE(entries.size() == 3);
        REQUIRE(entries[0].first == "a");
        REQUIRE(entries[0].second == "1");
        REQUIRE(entries[1].first == "b");
        REQUIRE(entries[1].second == "2");
        REQUIRE(entries[2].first == "c");
        REQUIRE(entries[2].second == "3");
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
            std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
            int overflow(int) override { return traits_type::eof(); }
        } buffer;
        std::ostream failing_stream(&buffer);

        REQUIRE_FALSE(memtable.write_to(failing_stream));
    }
}
