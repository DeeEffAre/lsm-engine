#include "storage_engine/lsm_tree.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("SSTable and Serialization Mechanism", "[sstable]") {
    const std::string test_path = "test_sstable_data.bin";

    // Clean up before starting the tests just in case
    if (fs::exists(test_path)) {
        fs::remove(test_path);
    }

    SECTION("SSTable Write and Read with Complex Types (std::string)") {
        DB<std::string, std::string> db;
        db.put("k1", "v1");
        db.put("k2", "v2");
        db.put("k3", "v3");

        auto sstable = db.write_sstable(test_path);
        REQUIRE(sstable != nullptr);
        REQUIRE(fs::exists(test_path));

        // Disk lookup tests
        auto res1 = sstable->get("k1");
        auto res2 = sstable->get("k3");
        auto res_null = sstable->get("non_existent");

        REQUIRE(res1 != nullptr);
        REQUIRE(*res1 == "v1");

        REQUIRE(res2 != nullptr);
        REQUIRE(*res2 == "v3");

        REQUIRE(res_null == nullptr);
    }

    SECTION("SSTable Write and Read with Primitive Types (int, double)") {
        DB<int, double> db;
        db.put(1, 10.5);
        db.put(2, 20.5);

        auto sstable = db.write_sstable(test_path);
        REQUIRE(sstable != nullptr);

        auto res1 = sstable->get(1);
        auto res2 = sstable->get(2);
        auto res_null = sstable->get(999);

        REQUIRE(res1 != nullptr);
        REQUIRE(*res1 == 10.5);

        REQUIRE(res2 != nullptr);
        REQUIRE(*res2 == 20.5);

        REQUIRE(res_null == nullptr);
    }

    SECTION("Should return nullptr when write_sstable fails due to an invalid path") {
        DB<int, int> db;
        db.put(1, 1);

        // Attempting to write to an invalid/non-existent directory path
        auto sstable = db.write_sstable("/invalid_directory_xyz/data.bin");
        REQUIRE(sstable == nullptr);
    }

    // Clean up artifact files after tests finish
    if (fs::exists(test_path)) {
        fs::remove(test_path);
    }
}
