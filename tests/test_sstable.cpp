#include "storage_engine/lsm_tree.hpp"

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
        Database<std::string, std::string> db;
        db.put("k1", "v1");
        db.put("k2", "v2");
        db.put("k3", "v3");

        auto sstable = db.get_db().write_sstable(test_path);
        REQUIRE(sstable != nullptr);
        REQUIRE(fs::exists(test_path));

        // Disk lookup tests: SSTable works on raw bytes, so keys/values
        // must be (de)serialized explicitly.
        ByteVec value;

        REQUIRE(sstable->get(serialize_value<std::string>("k1"), value));
        REQUIRE(deserialize_value<std::string>(value, static_cast<uint32_t>(value.size())) == "v1");

        REQUIRE(sstable->get(serialize_value<std::string>("k3"), value));
        REQUIRE(deserialize_value<std::string>(value, static_cast<uint32_t>(value.size())) == "v3");

        REQUIRE_FALSE(sstable->get(serialize_value<std::string>("non_existent"), value));
    }

    SECTION("SSTable Write and Read with Primitive Types (int, double)") {
        Database<int, double> db;
        db.put(1, 10.5);
        db.put(2, 20.5);

        auto sstable = db.get_db().write_sstable(test_path);
        REQUIRE(sstable != nullptr);

        ByteVec value;

        REQUIRE(sstable->get(serialize_value<int>(1), value));
        REQUIRE(deserialize_value<double>(value, static_cast<uint32_t>(value.size())) == 10.5);

        REQUIRE(sstable->get(serialize_value<int>(2), value));
        REQUIRE(deserialize_value<double>(value, static_cast<uint32_t>(value.size())) == 20.5);

        REQUIRE_FALSE(sstable->get(serialize_value<int>(999), value));
    }

    SECTION("Should return nullptr when write_sstable fails due to an invalid path") {
        Database<int, int> db;
        db.put(1, 1);

        // Attempting to write to an invalid/non-existent directory path
        auto sstable = db.get_db().write_sstable("/invalid_directory_xyz/data.bin");
        REQUIRE(sstable == nullptr);
    }

    // Clean up artifact files after tests finish
    if (fs::exists(test_path)) {
        fs::remove(test_path);
    }
}
