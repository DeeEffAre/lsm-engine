#include "storage_engine/engine.hpp"
#include "storage_engine/serialize_deserialize.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

void remove_if_exists(const std::string& path) {
    if (fs::exists(path)) {
        fs::remove(path);
    }
}

void remove_database_artifacts() {
    remove_if_exists("db.wal");

    std::vector<fs::path> sstables;
    for (const auto& entry : fs::directory_iterator(".")) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with("data-") && name.ends_with(".sstable")) {
            sstables.push_back(entry.path());
        }
    }

    for (const auto& path : sstables) {
        fs::remove(path);
    }
}

// Removes artifacts both before and after a test, even when an assertion fails.
struct DatabaseArtifactsGuard {
    DatabaseArtifactsGuard() { remove_database_artifacts(); }
    ~DatabaseArtifactsGuard() { remove_database_artifacts(); }

    DatabaseArtifactsGuard(const DatabaseArtifactsGuard&) = delete;
    DatabaseArtifactsGuard& operator=(const DatabaseArtifactsGuard&) = delete;
};

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
    engine.put(RecordType::PUT, serialized_key<std::string>("key"), *value_serialized);

    const Engine& const_engine = engine;
    auto value_expected = const_engine.get(serialized_key<std::string>("key"));
    REQUIRE(value_expected.has_value());
    REQUIRE(*value_expected != nullptr);

    const ByteVec* value = *value_expected;
    const auto decoded =
        deserialize_value<std::string>(*value, static_cast<uint32_t>(value->size()));
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "value");

    auto missing = const_engine.get(serialized_key<std::string>("missing"));
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error() == ErrorCode::NOT_FOUND);

    auto table = engine.write_sstable(path.c_str());
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
        auto before_flush = db.get_db().get(serialized_key<std::string>("a"));
        REQUIRE(before_flush.has_value());
        REQUIRE(*before_flush != nullptr);

        db.put("b", "2");

        // After the flush the memtable must be empty...
        auto after_a = db.get_db().get(serialized_key<std::string>("a"));
        REQUIRE_FALSE(after_a.has_value());
        REQUIRE(after_a.error() == ErrorCode::NOT_FOUND);

        auto after_b = db.get_db().get(serialized_key<std::string>("b"));
        REQUIRE_FALSE(after_b.has_value());
        REQUIRE(after_b.error() == ErrorCode::NOT_FOUND);

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

TEST_CASE("Database truncates a torn WAL tail on recovery", "[engine][wal][recovery]") {
    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");

    std::size_t valid_wal_size = 0;
    {
        Database<std::string, std::string> db{100};
        REQUIRE(db.put("a", "1"));
        REQUIRE(db.put("b", "2"));
        valid_wal_size = fs::file_size("db.wal");
    }

    // Simulate a crash while appending a third record: only a partial key exists.
    {
        std::ofstream wal("db.wal", std::ios::binary | std::ios::app);
        const ByteVec torn_bytes{std::byte{0x05}, std::byte{'c'}}; // key length 5, one byte
        wal.write(reinterpret_cast<const char*>(torn_bytes.data()),
                  static_cast<std::streamsize>(torn_bytes.size()));
        wal.flush();
    }
    REQUIRE(fs::file_size("db.wal") > valid_wal_size);

    {
        Database<std::string, std::string> recovered{100};

        auto a = recovered.get("a");
        REQUIRE(a != nullptr);
        REQUIRE(*a == "1");

        // The torn bytes must have been truncated away during recovery.
        REQUIRE(fs::file_size("db.wal") == valid_wal_size);

        // A write after truncation must survive another restart; without the
        // truncation it would be appended after the torn bytes and never replayed.
        REQUIRE(recovered.put("c", "3"));
    }

    {
        Database<std::string, std::string> reopened{100};
        auto c = reopened.get("c");
        REQUIRE(c != nullptr);
        REQUIRE(*c == "3");
    }

    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
}

TEST_CASE("Flushed data survives a restart", "[engine][recovery]") {
    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
    remove_if_exists("data-1.sstable");

    {
        Database<std::string, std::string> db{2};
        REQUIRE(db.put("a", "1"));
        REQUIRE(db.put("b", "2")); // reaches the threshold -> flush to data-0.sstable
        REQUIRE(fs::exists("data-0.sstable"));
    }

    {
        Database<std::string, std::string> reopened{100};

        auto a = reopened.get("a");
        auto b = reopened.get("b");
        REQUIRE(a != nullptr);
        REQUIRE(*a == "1");
        REQUIRE(b != nullptr);
        REQUIRE(*b == "2");
        REQUIRE(reopened.get("missing") == nullptr);
    }

    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
    remove_if_exists("data-1.sstable");
}

TEST_CASE("SSTable counter is recovered across restarts", "[engine][recovery][flush]") {
    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
    remove_if_exists("data-1.sstable");

    {
        Database<std::string, std::string> db{1};
        REQUIRE(db.put("a", "1")); // flush -> data-0.sstable
        REQUIRE(fs::exists("data-0.sstable"));
    }

    {
        Database<std::string, std::string> db{1};
        REQUIRE(db.put("b", "2")); // must land in data-1.sstable
        REQUIRE(fs::exists("data-1.sstable"));
    }

    // data-0 must not have been overwritten by the second flush
    const SSTable first("data-0.sstable");
    REQUIRE(first.get(serialized_key<std::string>("a")).has_value());
    REQUIRE_FALSE(first.get(serialized_key<std::string>("b")).has_value());

    const SSTable second("data-1.sstable");
    REQUIRE(second.get(serialized_key<std::string>("b")).has_value());
    REQUIRE_FALSE(second.get(serialized_key<std::string>("a")).has_value());

    {
        Database<std::string, std::string> reopened{100};
        auto a = reopened.get("a");
        auto b = reopened.get("b");
        REQUIRE(a != nullptr);
        REQUIRE(*a == "1");
        REQUIRE(b != nullptr);
        REQUIRE(*b == "2");
    }

    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
    remove_if_exists("data-1.sstable");
}

TEST_CASE("Latest value wins across recovered sstables", "[engine][recovery]") {
    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
    remove_if_exists("data-1.sstable");

    {
        Database<std::string, std::string> db{1};
        REQUIRE(db.put("key", "old")); // data-0.sstable
        REQUIRE(db.put("key", "new")); // data-1.sstable
    }

    {
        Database<std::string, std::string> reopened{100};

        auto value = reopened.get("key");
        REQUIRE(value != nullptr);
        REQUIRE(*value == "new");
    }

    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
    remove_if_exists("data-1.sstable");
}

TEST_CASE("Database tombstones hide older values", "[engine][tombstone]") {
    DatabaseArtifactsGuard guard;

    SECTION("Delete hides a value in the memtable") {
        Database<std::string, std::string> db{100};
        REQUIRE(db.put("key", "value"));
        REQUIRE(db.delete_record("key"));
        REQUIRE(db.get("key") == nullptr);
    }

    SECTION("Deleting a missing key succeeds and stays missing") {
        Database<std::string, std::string> db{100};
        REQUIRE(db.delete_record("missing"));
        REQUIRE(db.get("missing") == nullptr);
    }

    SECTION("Delete hides a value from an older sstable") {
        Database<std::string, std::string> db{1};
        REQUIRE(db.put("key", "old")); // flush -> data-0.sstable
        REQUIRE(fs::exists("data-0.sstable"));

        REQUIRE(db.delete_record("key"));
        REQUIRE(db.get("key") == nullptr);
    }

    SECTION("A PUT after a DELETE resurrects the key") {
        Database<std::string, std::string> db{100};
        REQUIRE(db.put("key", "old"));
        REQUIRE(db.delete_record("key"));
        REQUIRE(db.get("key") == nullptr);

        REQUIRE(db.put("key", "new"));
        auto value = db.get("key");
        REQUIRE(value != nullptr);
        REQUIRE(*value == "new");
    }

    SECTION("handle dispatches PUT and DELETE") {
        Database<std::string, std::string> db{100};
        REQUIRE(db.handle(RecordType::PUT, std::string{"key"}, std::string{"value"}));

        auto value = db.get("key");
        REQUIRE(value != nullptr);
        REQUIRE(*value == "value");

        REQUIRE(db.handle(RecordType::DELETE, std::string{"key"}, std::string{}));
        REQUIRE(db.get("key") == nullptr);
    }
}

TEST_CASE("Database tombstones count as memtable writes", "[engine][tombstone][flush]") {
    DatabaseArtifactsGuard guard;

    SECTION("A delete alone can trigger a flush") {
        Database<std::string, std::string> db{1};
        REQUIRE(db.delete_record("missing"));

        REQUIRE(fs::exists("data-0.sstable"));
        REQUIRE(db.get("missing") == nullptr);

        const SSTable table("data-0.sstable");
        auto deleted = table.get(serialized_key<std::string>("missing"));
        REQUIRE_FALSE(deleted.has_value());
        REQUIRE(deleted.error() == ErrorCode::KEY_DELETED);
    }

    SECTION("A delete after a put reaches the threshold") {
        Database<std::string, std::string> db{2};
        REQUIRE(db.put("a", "1"));
        REQUIRE_FALSE(fs::exists("data-0.sstable"));

        REQUIRE(db.delete_record("b"));
        REQUIRE(fs::exists("data-0.sstable"));

        auto a = db.get("a");
        REQUIRE(a != nullptr);
        REQUIRE(*a == "1");
        REQUIRE(db.get("b") == nullptr);
    }
}

TEST_CASE("Database tombstones survive recovery", "[engine][tombstone][recovery]") {
    DatabaseArtifactsGuard guard;

    SECTION("A tombstone replayed from the WAL hides an older sstable value") {
        {
            Database<std::string, std::string> db{1};
            REQUIRE(db.put("key", "old")); // flush -> data-0.sstable, WAL cleared
            REQUIRE(fs::exists("data-0.sstable"));
        }

        {
            Database<std::string, std::string> db{100};
            REQUIRE(db.delete_record("key")); // stays in the WAL
            REQUIRE(db.get("key") == nullptr);
        }

        {
            Database<std::string, std::string> reopened{100};
            REQUIRE(reopened.get("key") == nullptr);
        }
    }

    SECTION("A flushed tombstone survives a restart") {
        {
            Database<std::string, std::string> db{1};
            REQUIRE(db.put("key", "old"));    // flush -> data-0.sstable
            REQUIRE(db.delete_record("key")); // must flush -> data-1.sstable
        }

        REQUIRE(fs::exists("data-1.sstable"));
        const SSTable tombstone("data-1.sstable");
        auto deleted = tombstone.get(serialized_key<std::string>("key"));
        REQUIRE_FALSE(deleted.has_value());
        REQUIRE(deleted.error() == ErrorCode::KEY_DELETED);

        Database<std::string, std::string> reopened{100};
        REQUIRE(reopened.get("key") == nullptr);
    }

    SECTION("A key can be revived after a delete was flushed") {
        {
            Database<std::string, std::string> db{1};
            REQUIRE(db.put("key", "old"));    // data-0.sstable
            REQUIRE(db.delete_record("key")); // data-1.sstable
            REQUIRE(db.put("key", "new"));    // data-2.sstable
        }

        Database<std::string, std::string> reopened{100};
        auto value = reopened.get("key");
        REQUIRE(value != nullptr);
        REQUIRE(*value == "new");
    }
}

TEST_CASE("WAL and sstables recover together", "[engine][recovery][wal]") {
    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
    remove_if_exists("data-1.sstable");

    {
        Database<std::string, std::string> db{2};
        REQUIRE(db.put("a", "1"));
        REQUIRE(db.put("b", "2")); // flush -> data-0.sstable, WAL cleared
        REQUIRE(fs::exists("data-0.sstable"));
        REQUIRE(db.put("c", "3")); // stays in the WAL
    }

    {
        Database<std::string, std::string> reopened{100};

        auto a = reopened.get("a"); // from data-0.sstable
        auto b = reopened.get("b"); // from data-0.sstable
        auto c = reopened.get("c"); // from the replayed WAL
        REQUIRE(a != nullptr);
        REQUIRE(*a == "1");
        REQUIRE(b != nullptr);
        REQUIRE(*b == "2");
        REQUIRE(c != nullptr);
        REQUIRE(*c == "3");
    }

    remove_if_exists("db.wal");
    remove_if_exists("data-0.sstable");
    remove_if_exists("data-1.sstable");
}
