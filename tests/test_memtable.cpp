#include "storage_engine/lsm_tree.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Memtable Basic Operations", "[memtable]") {
    Memtable<std::string, std::string> memtable;

    SECTION("Empty memtable should return nullptr") {
        REQUIRE(memtable.get("non_existent_key") == nullptr);
    }

    SECTION("Should insert data and retrieve it correctly") {
        memtable.put("key1", "value1");
        memtable.put("key2", "value2");

        const std::string* v1 = memtable.get("key1");
        const std::string* v2 = memtable.get("key2");

        REQUIRE(v1 != nullptr);
        REQUIRE(*v1 == "value1");
        REQUIRE(v2 != nullptr);
        REQUIRE(*v2 == "value2");
    }

    SECTION("Should overwrite value when putting the same key twice") {
        memtable.put("key", "old_value");
        memtable.put("key", "new_value");

        const std::string* v = memtable.get("key");
        REQUIRE(v != nullptr);
        REQUIRE(*v == "new_value");
    }
}
