#include "storage_engine/engine.hpp"
#include "storage_engine/serialize_deserialize.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("SSTable and Serialization Mechanism", "[sstable]") {
    const std::string test_path = "test_sstable_data.bin";

    // Clean up before starting the tests just in case
    if (fs::exists(test_path)) {
        fs::remove(test_path);
    }

    SECTION("SSTable Write and Read with Complex Types (std::string)") {
        Database<std::string, std::string> db{100};
        db.put("k1", "v1");
        db.put("k2", "v2");
        db.put("k3", "v3");

        auto sstable = db.get_db().write_sstable(test_path);
        REQUIRE(sstable.has_value());
        REQUIRE(fs::exists(test_path));

        // Disk lookup tests: keys are raw byte views, values are encoded records.
        auto res1 = sstable->get(serialize_value<std::string>("k1"));
        REQUIRE(res1.has_value());
        const auto value1 = decode<std::string>(*res1);
        REQUIRE(value1.has_value());
        REQUIRE(*value1 == "v1");

        auto res3 = sstable->get(serialize_value<std::string>("k3"));
        REQUIRE(res3.has_value());
        const auto value3 = decode<std::string>(*res3);
        REQUIRE(value3.has_value());
        REQUIRE(*value3 == "v3");

        auto res_null = sstable->get(serialize_value<std::string>("non_existent"));
        REQUIRE_FALSE(res_null.has_value());
        REQUIRE(res_null.error() == SSTableError::NOT_FOUND);
    }

    SECTION("SSTable Write and Read with Primitive Types (int, double)") {
        Database<int, double> db{100};
        db.put(1, 10.5);
        db.put(2, 20.5);

        auto sstable = db.get_db().write_sstable(test_path);
        REQUIRE(sstable.has_value());

        auto res1 = sstable->get(serialize_value<int>(1));
        REQUIRE(res1.has_value());
        const auto value1 = decode<double>(*res1);
        REQUIRE(value1.has_value());
        REQUIRE(*value1 == 10.5);

        auto res2 = sstable->get(serialize_value<int>(2));
        REQUIRE(res2.has_value());
        const auto value2 = decode<double>(*res2);
        REQUIRE(value2.has_value());
        REQUIRE(*value2 == 20.5);

        auto res_null = sstable->get(serialize_value<int>(999));
        REQUIRE_FALSE(res_null.has_value());
        REQUIRE(res_null.error() == SSTableError::NOT_FOUND);
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
}

TEST_CASE("SSTable error handling", "[sstable]") {
    SECTION("Missing file returns FILE_ERROR") {
        const SSTable table("test_sstable_missing_file.bin");

        auto result = table.get(serialize_value<std::string>("key"));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == SSTableError::FILE_ERROR);
    }
}

TEST_CASE("SSTable handles empty values and empty tables", "[sstable]") {
    const std::string path = "test_sstable_edge_cases.bin";

    if (fs::exists(path)) {
        fs::remove(path);
    }

    SECTION("Empty values are returned as found entries") {
        const auto empty_encoded = encode<std::string>("");
        const auto full_encoded = encode<std::string>("value");
        REQUIRE(empty_encoded.has_value());
        REQUIRE(full_encoded.has_value());

        Engine engine;
        engine.put(serialize_value<std::string>("empty"), *empty_encoded);
        engine.put(serialize_value<std::string>("full"), *full_encoded);

        auto table = engine.write_sstable(path);
        REQUIRE(table.has_value());

        auto empty = table->get(serialize_value<std::string>("empty"));
        REQUIRE(empty.has_value());
        const auto empty_decoded = decode<std::string>(*empty);
        REQUIRE(empty_decoded.has_value());
        REQUIRE(empty_decoded->empty());

        auto full = table->get(serialize_value<std::string>("full"));
        REQUIRE(full.has_value());
        const auto full_decoded = decode<std::string>(*full);
        REQUIRE(full_decoded.has_value());
        REQUIRE(*full_decoded == "value");
    }

    SECTION("Flushing an empty memtable yields a table where every key is missing") {
        Engine engine;

        auto table = engine.write_sstable(path);
        REQUIRE(table.has_value());

        auto result = table->get(serialize_value<std::string>("anything"));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == SSTableError::NOT_FOUND);
    }

    if (fs::exists(path)) {
        fs::remove(path);
    }
}
