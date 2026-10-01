#include "storage_engine/engine.hpp"
#include "storage_engine/serialize_deserialize.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

void remove_if_exists(const std::string& path) {
    if (fs::exists(path)) {
        fs::remove(path);
    }
}

} // namespace

TEST_CASE("Engine integrates memtable and sstable", "[engine]") {
    const std::string path = "test_engine_data.bin";
    remove_if_exists(path);

    const auto value_encoded = encode<std::string>("value");
    REQUIRE(value_encoded.has_value());

    Engine engine;
    engine.put(serialize_value<std::string>("key"), *value_encoded);

    const Engine& const_engine = engine;
    const ByteVec* value = const_engine.get(serialize_value<std::string>("key"));
    REQUIRE(value != nullptr);
    const auto decoded = decode<std::string>(*value);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "value");
    REQUIRE(const_engine.get(serialize_value<std::string>("missing")) == nullptr);

    auto table = engine.write_sstable(path);
    REQUIRE(table.has_value());
    REQUIRE(fs::exists(path));

    auto from_disk = table->get(serialize_value<std::string>("key"));
    REQUIRE(from_disk.has_value());
    const auto from_disk_decoded = decode<std::string>(*from_disk);
    REQUIRE(from_disk_decoded.has_value());
    REQUIRE(*from_disk_decoded == "value");

    remove_if_exists(path);
}

TEST_CASE("Database typed access", "[engine]") {
    SECTION("Typed access works through the Database wrapper") {
        Database<int, double> db{100};
        db.put(1, 10.5);

        auto value = db.get(1);
        REQUIRE(value != nullptr);
        REQUIRE(*value == 10.5);
        REQUIRE(db.get(2) == nullptr);
    }

    SECTION("put returns true on success") {
        Database<std::string, std::string> db{100};
        REQUIRE(db.put("key", "value"));
    }
}

TEST_CASE("Database flush behavior", "[engine][flush]") {
    remove_if_exists("data-0.sstable");
    remove_if_exists("data-1.sstable");

    SECTION("Reaching max_memtable_size flushes the memtable to data-0.sstable") {
        Database<std::string, std::string> db{2};
        db.put("a", "1");
        db.put("b", "2");

        REQUIRE(fs::exists("data-0.sstable"));

        auto a = db.get("a");
        auto b = db.get("b");
        REQUIRE(a != nullptr);
        REQUIRE(*a == "1");
        REQUIRE(b != nullptr);
        REQUIRE(*b == "2");
        REQUIRE(db.get("missing") == nullptr);
    }

    SECTION("Below the threshold no sstable file is created") {
        Database<std::string, std::string> db{100};
        db.put("a", "1");
        db.put("b", "2");

        REQUIRE_FALSE(fs::exists("data-0.sstable"));
    }

    SECTION("Consecutive flushes create distinct sstable files") {
        Database<std::string, std::string> db{1};
        db.put("a", "1");
        db.put("b", "2");

        REQUIRE(fs::exists("data-0.sstable"));
        REQUIRE(fs::exists("data-1.sstable"));
    }

    SECTION("Flush clears the memtable after writing") {
        Database<std::string, std::string> db{2};
        db.put("a", "1");

        // Before the flush the value only lives in the memtable.
        REQUIRE(db.get_db().get(serialize_value<std::string>("a")) != nullptr);

        db.put("b", "2");

        // After the flush the memtable must be empty...
        REQUIRE(db.get_db().get(serialize_value<std::string>("a")) == nullptr);
        REQUIRE(db.get_db().get(serialize_value<std::string>("b")) == nullptr);

        // ...but the values must still be reachable through the sstable.
        auto a = db.get("a");
        REQUIRE(a != nullptr);
        REQUIRE(*a == "1");
    }

    SECTION("Each flush writes only the current memtable batch") {
        Database<std::string, std::string> db{1};
        db.put("a", "1");
        db.put("b", "2");

        const SSTable first("data-0.sstable");
        REQUIRE(first.get(serialize_value<std::string>("a")).has_value());
        REQUIRE_FALSE(first.get(serialize_value<std::string>("b")).has_value());

        const SSTable second("data-1.sstable");
        REQUIRE(second.get(serialize_value<std::string>("b")).has_value());
        REQUIRE_FALSE(second.get(serialize_value<std::string>("a")).has_value());
    }

    SECTION("Overwritten keys are read from the newest sstable") {
        Database<std::string, std::string> db{1};
        db.put("key", "old");
        db.put("key", "new");

        auto value = db.get("key");
        REQUIRE(value != nullptr);
        REQUIRE(*value == "new");
    }

    SECTION("Keys from older sstables are still found") {
        Database<std::string, std::string> db{1};
        db.put("a", "1");
        db.put("b", "2");

        auto a = db.get("a");
        REQUIRE(a != nullptr);
        REQUIRE(*a == "1");
    }

    remove_if_exists("data-0.sstable");
    remove_if_exists("data-1.sstable");
}
