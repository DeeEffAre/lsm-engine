#include "storage_engine/engine.hpp"
#include "storage_engine/serialize_deserialize.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

void remove_if_exists(const std::string& path) {
    if (fs::exists(path)) {
        fs::remove(path);
    }
}

template <typename T>
ByteVec serialized_key(const T& value) {
    auto encoded = serialize_value(value);
    REQUIRE(encoded.has_value());
    return *encoded;
}

} // namespace

TEST_CASE("Engine integrates memtable and sstable", "[engine]") {
    const std::string path = "test_engine_data.bin";
    remove_if_exists(path);

    const auto value_serialized = serialize_value<std::string>("value");
    REQUIRE(value_serialized.has_value());

    Engine engine;
    engine.put(serialized_key<std::string>("key"), *value_serialized);

    const Engine& const_engine = engine;
    const ByteVec* value = const_engine.get(serialized_key<std::string>("key"));
    REQUIRE(value != nullptr);
    const auto decoded = deserialize_value<std::string>(*value, static_cast<uint32_t>(value->size()));
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "value");
    REQUIRE(const_engine.get(serialized_key<std::string>("missing")) == nullptr);

    auto table = engine.write_sstable(path);
    REQUIRE(table.has_value());
    REQUIRE(fs::exists(path));

    auto from_disk = table->get(serialized_key<std::string>("key"));
    REQUIRE(from_disk.has_value());
    const auto from_disk_decoded =
        deserialize_value<std::string>(*from_disk, static_cast<uint32_t>(from_disk->size()));
    REQUIRE(from_disk_decoded.has_value());
    REQUIRE(*from_disk_decoded == "value");

    remove_if_exists(path);
}

TEST_CASE("Database typed access", "[engine]") {
    remove_if_exists("db.wal");

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

    remove_if_exists("db.wal");
}

TEST_CASE("Database flush behavior", "[engine][flush]") {
    remove_if_exists("db.wal");
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
        REQUIRE(db.get_db().get(serialized_key<std::string>("a")) != nullptr);

        db.put("b", "2");

        // After the flush the memtable must be empty...
        REQUIRE(db.get_db().get(serialized_key<std::string>("a")) == nullptr);
        REQUIRE(db.get_db().get(serialized_key<std::string>("b")) == nullptr);

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
        REQUIRE(first.get(serialized_key<std::string>("a")).has_value());
        REQUIRE_FALSE(first.get(serialized_key<std::string>("b")).has_value());

        const SSTable second("data-1.sstable");
        REQUIRE(second.get(serialized_key<std::string>("b")).has_value());
        REQUIRE_FALSE(second.get(serialized_key<std::string>("a")).has_value());
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

    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
    remove_if_exists("data-1.sstable");
}

TEST_CASE("Database recovers committed writes from the WAL", "[engine][wal]") {
    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");

    SECTION("Committed writes survive a restart") {
        {
            Database<std::string, std::string> db{100};
            REQUIRE(db.put("key", "value"));
        }

        Database<std::string, std::string> reopened{100};

        auto value = reopened.get("key");
        REQUIRE(value != nullptr);
        REQUIRE(*value == "value");
        REQUIRE(reopened.get("missing") == nullptr);
    }

    SECTION("Recovery keeps the latest value of an overwritten key") {
        {
            Database<std::string, std::string> db{100};
            REQUIRE(db.put("key", "old"));
            REQUIRE(db.put("key", "new"));
        }

        Database<std::string, std::string> reopened{100};

        auto value = reopened.get("key");
        REQUIRE(value != nullptr);
        REQUIRE(*value == "new");
    }

    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
}

TEST_CASE("Flushing the memtable clears the WAL", "[engine][wal][flush]") {
    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");

    Database<std::string, std::string> db{2};
    REQUIRE(db.put("a", "1"));
    REQUIRE(db.put("b", "2")); // threshold reached -> flush

    REQUIRE(fs::exists("data-0.sstable"));
    REQUIRE(fs::exists("db.wal"));
    REQUIRE(fs::file_size("db.wal") == 0);

    // Values must still be reachable, now through the sstable.
    auto a = db.get("a");
    REQUIRE(a != nullptr);
    REQUIRE(*a == "1");

    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
}

TEST_CASE("Recovered entries count towards the memtable threshold", "[engine][wal][flush]") {
    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");

    {
        Database<std::string, std::string> db{3};
        REQUIRE(db.put("a", "1"));
        REQUIRE(db.put("b", "2"));
        REQUIRE_FALSE(fs::exists("data-0.sstable"));
    }

    {
        Database<std::string, std::string> db{3}; // restores 2 entries from the WAL
        REQUIRE(db.put("c", "3"));                // third entry reaches the threshold
        REQUIRE(fs::exists("data-0.sstable"));
    }

    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
}
