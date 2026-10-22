#include "storage_engine/engine.hpp"
#include "storage_engine/serialize_deserialize.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

template <typename T>
ByteVec serialized_key(const T& value) {
    auto encoded = serialize_value(value);
    REQUIRE(encoded.has_value());
    return *encoded;
}

template <typename T>
ByteVec serialized_value(const T& value) {
    auto encoded = serialize_value(value);
    REQUIRE(encoded.has_value());
    return *encoded;
}

} // namespace

TEST_CASE("SSTable and Serialization Mechanism", "[sstable]") {
    const std::string test_path = "test_sstable_data.bin";

    // Clean up before starting the tests just in case
    if (fs::exists(test_path)) {
        fs::remove(test_path);
    }
    if (fs::exists("db.wal")) {
        fs::remove("db.wal");
    }

    SECTION("SSTable Write and Read with Complex Types (std::string)") {
        Database<std::string, std::string> db{100};
        db.put("k1", "v1");
        db.put("k2", "v2");
        db.put("k3", "v3");

        auto sstable = db.get_db().write_sstable(test_path.c_str());
        REQUIRE(sstable.has_value());
        REQUIRE(fs::exists(test_path));

        // Disk lookup tests: both keys and values are raw (serialized) bytes.
        auto res1 = sstable->get(serialized_key<std::string>("k1"));
        REQUIRE(res1.has_value());
        const auto value1 =
            deserialize_value<std::string>(*res1, static_cast<uint32_t>(res1->size()));
        REQUIRE(value1.has_value());
        REQUIRE(*value1 == "v1");

        auto res3 = sstable->get(serialized_key<std::string>("k3"));
        REQUIRE(res3.has_value());
        const auto value3 =
            deserialize_value<std::string>(*res3, static_cast<uint32_t>(res3->size()));
        REQUIRE(value3.has_value());
        REQUIRE(*value3 == "v3");

        auto res_null = sstable->get(serialized_key<std::string>("non_existent"));
        REQUIRE_FALSE(res_null.has_value());
        REQUIRE(res_null.error() == ErrorCode::NOT_FOUND);
    }

    SECTION("SSTable Write and Read with Primitive Types (int, double)") {
        Database<int, double> db{100};
        db.put(1, 10.5);
        db.put(2, 20.5);

        auto sstable = db.get_db().write_sstable(test_path.c_str());
        REQUIRE(sstable.has_value());

        auto res1 = sstable->get(serialized_key<int>(1));
        REQUIRE(res1.has_value());
        const auto value1 = deserialize_value<double>(*res1, static_cast<uint32_t>(res1->size()));
        REQUIRE(value1.has_value());
        REQUIRE(*value1 == 10.5);

        auto res2 = sstable->get(serialized_key<int>(2));
        REQUIRE(res2.has_value());
        const auto value2 = deserialize_value<double>(*res2, static_cast<uint32_t>(res2->size()));
        REQUIRE(value2.has_value());
        REQUIRE(*value2 == 20.5);

        auto res_null = sstable->get(serialized_key<int>(999));
        REQUIRE_FALSE(res_null.has_value());
        REQUIRE(res_null.error() == ErrorCode::NOT_FOUND);
    }

    SECTION("Should return an error when write_sstable fails due to an invalid path") {
        Database<int, int> db{100};
        db.put(1, 1);

        // Attempting to write to an invalid/non-existent directory path
        auto sstable = db.get_db().write_sstable("/invalid_directory_xyz/data.bin");
        REQUIRE_FALSE(sstable.has_value());
    }

    // Clean up artifact files after tests finish
    if (fs::exists(test_path)) {
        fs::remove(test_path);
    }
    if (fs::exists("db.wal")) {
        fs::remove("db.wal");
    }
}

TEST_CASE("SSTable error handling", "[sstable]") {
    SECTION("Missing file returns FILE_ERROR") {
        const SSTable table("test_sstable_missing_file.bin");

        auto result = table.get(serialized_key<std::string>("key"));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == ErrorCode::FILE_ERROR);
    }
}

TEST_CASE("SSTable handles empty values and empty tables", "[sstable]") {
    const std::string path = "test_sstable_edge_cases.bin";

    if (fs::exists(path)) {
        fs::remove(path);
    }

    SECTION("Empty values are returned as found entries") {
        const auto empty_serialized = serialize_value<std::string>("");
        const auto full_serialized = serialize_value<std::string>("value");
        REQUIRE(empty_serialized.has_value());
        REQUIRE(full_serialized.has_value());

        Engine engine;
        engine.put(RecordType::PUT, serialized_key<std::string>("empty"), *empty_serialized);
        engine.put(RecordType::PUT, serialized_key<std::string>("full"), *full_serialized);

        auto table = engine.write_sstable(path.c_str());
        REQUIRE(table.has_value());

        auto empty = table->get(serialized_key<std::string>("empty"));
        REQUIRE(empty.has_value());
        const auto empty_decoded =
            deserialize_value<std::string>(*empty, static_cast<uint32_t>(empty->size()));
        REQUIRE(empty_decoded.has_value());
        REQUIRE(empty_decoded->empty());

        auto full = table->get(serialized_key<std::string>("full"));
        REQUIRE(full.has_value());
        const auto full_decoded =
            deserialize_value<std::string>(*full, static_cast<uint32_t>(full->size()));
        REQUIRE(full_decoded.has_value());
        REQUIRE(*full_decoded == "value");
    }

    SECTION("Flushing an empty memtable yields a table where every key is missing") {
        Engine engine;

        auto table = engine.write_sstable(path.c_str());
        REQUIRE(table.has_value());

        auto result = table->get(serialized_key<std::string>("anything"));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == ErrorCode::NOT_FOUND);
    }

    if (fs::exists(path)) {
        fs::remove(path);
    }
}

TEST_CASE("SSTable tombstones", "[sstable][tombstone]") {
    const std::string path = "test_sstable_tombstones.bin";

    if (fs::exists(path)) {
        fs::remove(path);
    }

    SECTION("A matching tombstone is surfaced as KEY_DELETED") {
        Engine engine;
        engine.put(RecordType::PUT, serialized_key<std::string>("alive"),
                   serialized_value<std::string>("value"));
        engine.put(RecordType::DELETE, serialized_key<std::string>("dead"), ByteVec{});

        auto table = engine.write_sstable(path.c_str());
        REQUIRE(table.has_value());

        auto dead = table->get(serialized_key<std::string>("dead"));
        REQUIRE_FALSE(dead.has_value());
        REQUIRE(dead.error() == ErrorCode::KEY_DELETED);

        auto alive = table->get(serialized_key<std::string>("alive"));
        REQUIRE(alive.has_value());
        const auto decoded =
            deserialize_value<std::string>(*alive, static_cast<uint32_t>(alive->size()));
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == "value");
    }

    SECTION("Records after a tombstone remain readable") {
        Engine engine;
        // "a" sorts before the other keys, so the tombstone is written first.
        engine.put(RecordType::DELETE, serialized_key<std::string>("a"), ByteVec{});
        engine.put(RecordType::PUT, serialized_key<std::string>("b"),
                   serialized_value<std::string>("2"));
        engine.put(RecordType::PUT, serialized_key<std::string>("c"),
                   serialized_value<std::string>("3"));

        auto table = engine.write_sstable(path.c_str());
        REQUIRE(table.has_value());

        auto missing = table->get(serialized_key<std::string>("z"));
        REQUIRE_FALSE(missing.has_value());
        REQUIRE(missing.error() == ErrorCode::NOT_FOUND);

        auto b = table->get(serialized_key<std::string>("b"));
        REQUIRE(b.has_value());
        const auto b_decoded = deserialize_value<std::string>(*b, static_cast<uint32_t>(b->size()));
        REQUIRE(b_decoded.has_value());
        REQUIRE(*b_decoded == "2");

        auto c = table->get(serialized_key<std::string>("c"));
        REQUIRE(c.has_value());
        const auto c_decoded = deserialize_value<std::string>(*c, static_cast<uint32_t>(c->size()));
        REQUIRE(c_decoded.has_value());
        REQUIRE(*c_decoded == "3");

        auto a = table->get(serialized_key<std::string>("a"));
        REQUIRE_FALSE(a.has_value());
        REQUIRE(a.error() == ErrorCode::KEY_DELETED);
    }

    SECTION("A PUT after a DELETE replaces the tombstone") {
        Engine engine;
        engine.put(RecordType::DELETE, serialized_key<std::string>("key"), ByteVec{});
        engine.put(RecordType::PUT, serialized_key<std::string>("key"),
                   serialized_value<std::string>("revived"));

        auto table = engine.write_sstable(path.c_str());
        REQUIRE(table.has_value());

        auto value = table->get(serialized_key<std::string>("key"));
        REQUIRE(value.has_value());
        const auto decoded =
            deserialize_value<std::string>(*value, static_cast<uint32_t>(value->size()));
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == "revived");
    }

    if (fs::exists(path)) {
        fs::remove(path);
    }
}
