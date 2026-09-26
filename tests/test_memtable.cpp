#include "storage_engine/engine.hpp"

#include <catch2/catch_test_macros.hpp>

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
