#include "storage_engine/serialize_deserialize.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <numbers>
#include <string>

TEST_CASE("Serialization round-trips supported types", "[serialization]") {
    SECTION("std::string") {
        const std::string value = "hello world";
        const ByteVec encoded = serialize_value(value);

        REQUIRE(encoded.size() == value.size());
        REQUIRE(get_encoded_size(value) == encoded.size());
        REQUIRE(deserialize_value<std::string>(encoded, get_encoded_size(value)) == value);
    }

    SECTION("empty std::string") {
        const std::string value;
        const ByteVec encoded = serialize_value(value);

        REQUIRE(encoded.empty());
        REQUIRE(get_encoded_size(value) == 0);
        REQUIRE(deserialize_value<std::string>(encoded, 0).empty());
    }

    SECTION("std::string with embedded null bytes") {
        const std::string value("a\0b", 3);
        const ByteVec encoded = serialize_value(value);

        REQUIRE(encoded.size() == value.size());
        REQUIRE(deserialize_value<std::string>(encoded, get_encoded_size(value)) == value);
    }

    SECTION("int") {
        const int value = -123456;
        const ByteVec encoded = serialize_value(value);

        REQUIRE(encoded.size() == sizeof(int));
        REQUIRE(get_encoded_size(value) == sizeof(int));
        REQUIRE(deserialize_value<int>(encoded, get_encoded_size(value)) == value);
    }

    SECTION("uint32_t") {
        const uint32_t value = 0xDEADBEEF;
        const ByteVec encoded = serialize_value(value);

        REQUIRE(encoded.size() == sizeof(uint32_t));
        REQUIRE(get_encoded_size(value) == sizeof(uint32_t));
        REQUIRE(deserialize_value<uint32_t>(encoded, get_encoded_size(value)) == value);
    }

    SECTION("double") {
        const double value = std::numbers::pi;
        const ByteVec encoded = serialize_value(value);

        REQUIRE(encoded.size() == sizeof(double));
        REQUIRE(get_encoded_size(value) == sizeof(double));
        REQUIRE(deserialize_value<double>(encoded, get_encoded_size(value)) == value);
    }
}

TEST_CASE("serialize_view is a zero-copy view of the value", "[serialization]") {
    SECTION("std::string") {
        const std::string value = "view me";
        const ByteSpan view = serialize_view(value);

        REQUIRE(view.size() == value.size());
        REQUIRE(view.data() == reinterpret_cast<const std::byte*>(value.data()));
        REQUIRE(std::ranges::equal(view, serialize_value(value)));
    }

    SECTION("trivially copyable type") {
        const uint64_t value = 42;
        const ByteSpan view = serialize_view(value);

        REQUIRE(view.size() == sizeof(uint64_t));
        REQUIRE(view.data() == reinterpret_cast<const std::byte*>(&value));
        REQUIRE(std::ranges::equal(view, serialize_value(value)));
    }
}
