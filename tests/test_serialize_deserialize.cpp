#include "storage_engine/serialize_deserialize.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <string>
#include <string_view>
#include <type_traits>

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

TEST_CASE("deserialize_view returns views into the source buffer", "[serialization]") {
    SECTION("std::string view points at the buffer without copying") {
        const std::string original = "hello view";
        const ByteVec buffer = serialize_value(original);

        const std::string_view view = deserialize_view<std::string>(buffer);

        REQUIRE(view.data() == reinterpret_cast<const char*>(buffer.data()));
        REQUIRE(view.size() == buffer.size());
        REQUIRE(view == original);
    }

    SECTION("std::string view observes in-place mutations of the buffer") {
        ByteVec buffer = serialize_value<std::string>("hello");

        const std::string_view view = deserialize_view<std::string>(buffer);
        buffer[0] = static_cast<std::byte>('H');

        REQUIRE(view == "Hello");
    }

    SECTION("std::string view of an empty buffer is empty") {
        const ByteVec buffer = serialize_value<std::string>("");

        const std::string_view view = deserialize_view<std::string>(buffer);

        REQUIRE(view.empty());
        REQUIRE(view.data() == reinterpret_cast<const char*>(buffer.data()));
    }

    SECTION("ByteSpan overload views only the given span") {
        const ByteVec buffer = serialize_value<std::string>("prefix-view");
        const ByteSpan span = ByteSpan{buffer}.subspan(7);

        const std::string_view view = deserialize_view<std::string>(span);

        REQUIRE(view.data() == reinterpret_cast<const char*>(buffer.data() + 7));
        REQUIRE(view.size() == 4);
        REQUIRE(view == "view");
    }

    SECTION("trivially copyable view is a reference into the buffer") {
        const uint32_t original = 0xDEADBEEF;
        const ByteVec buffer = serialize_value(original);

        const uint32_t& view = deserialize_view<uint32_t>(buffer);

        REQUIRE(&view == reinterpret_cast<const uint32_t*>(buffer.data()));
        REQUIRE(view == original);
    }

    SECTION("trivially copyable view observes in-place mutations of the buffer") {
        ByteVec buffer = serialize_value<uint32_t>(0x11223344);

        const uint32_t& view = deserialize_view<uint32_t>(buffer);
        const uint32_t new_value = 0x55667788;
        std::memcpy(buffer.data(), &new_value, sizeof(new_value));

        REQUIRE(view == new_value);
    }
}

TEST_CASE("deserialize_view types are chosen at compile time", "[serialization]") {
    STATIC_REQUIRE(std::is_same_v<view_of_t<std::string>, std::string_view>);
    STATIC_REQUIRE(std::is_same_v<view_of_t<int>, const int&>);
}

TEST_CASE("deserialize_view does not track the lifetime of its buffer", "[serialization]") {
    SECTION("Reallocation moves the buffer but not the view") {
        ByteVec buffer = serialize_value<std::string>("do not outlive me");
        const std::string_view view = deserialize_view<std::string>(buffer);

        const char* const old_address = view.data();
        buffer.reserve(buffer.capacity() + 1);

        // The view object still stores the old address, but the buffer has moved
        // to new storage: the view is now dangling and must not be dereferenced.
        REQUIRE(view.data() == old_address);
        REQUIRE(reinterpret_cast<const char*>(buffer.data()) != old_address);

        // The only safe option is to take a fresh view from the live buffer.
        REQUIRE(deserialize_view<std::string>(buffer) == "do not outlive me");
    }
}

// Reading through a view after its buffer is destroyed is undefined behavior:
// nothing marks the view invalid, so the result depends on the allocator
// (stale bytes, garbage, a crash). This test is compiled only when explicitly
// requested; build with -DLSM_TEST_UNDEFINED_BEHAVIOR and run it under ASan to
// see the heap-use-after-free report.
#ifdef LSM_TEST_UNDEFINED_BEHAVIOR
TEST_CASE("Reading a dangling deserialize_view is undefined behavior", "[ub]") {
    std::string_view dangling;
    {
        const ByteVec buffer = serialize_value<std::string>("dangling view");
        dangling = deserialize_view<std::string>(buffer);
    }

    // `buffer` is gone; the following reads touch freed memory.
    REQUIRE(dangling.size() == 13);
    REQUIRE(dangling == "dangling view");
}
#endif

TEST_CASE("encode prefixes the payload with its length", "[serialization]") {
    SECTION("std::string") {
        const std::string value = "hello";
        const auto encoded = encode(value);
        REQUIRE(encoded.has_value());
        REQUIRE(encoded->size() == sizeof(uint32_t) + value.size());

        uint32_t length = 0;
        std::memcpy(&length, encoded->data(), sizeof(length));
        REQUIRE(length == value.size());
        REQUIRE(std::memcmp(encoded->data() + sizeof(uint32_t), value.data(), value.size()) == 0);
    }

    SECTION("ByteVec") {
        const ByteVec value = serialize_value<std::string>("bytes");
        const auto encoded = encode<ByteVec>(value);
        REQUIRE(encoded.has_value());
        REQUIRE(encoded->size() == sizeof(uint32_t) + value.size());

        uint32_t length = 0;
        std::memcpy(&length, encoded->data(), sizeof(length));
        REQUIRE(length == value.size());
    }
}

TEST_CASE("encode and decode round-trip supported types", "[serialization]") {
    SECTION("std::string") {
        const auto encoded = encode<std::string>("hello world");
        REQUIRE(encoded.has_value());

        const auto decoded = decode<std::string>(*encoded);
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == "hello world");
    }

    SECTION("empty std::string") {
        const auto encoded = encode<std::string>("");
        REQUIRE(encoded.has_value());
        REQUIRE(encoded->size() == sizeof(uint32_t));

        const auto decoded = decode<std::string>(*encoded);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->empty());
    }

    SECTION("std::string with embedded null bytes") {
        const std::string value("a\0b", 3);
        const auto encoded = encode(value);
        REQUIRE(encoded.has_value());

        const auto decoded = decode<std::string>(*encoded);
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == value);
    }

    SECTION("ByteVec") {
        const ByteVec value = serialize_value<std::string>("raw bytes");
        const auto encoded = encode<ByteVec>(value);
        REQUIRE(encoded.has_value());

        const auto decoded = decode<ByteVec>(*encoded);
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == value);
    }

    SECTION("empty ByteVec") {
        const auto encoded = encode<ByteVec>(ByteVec{});
        REQUIRE(encoded.has_value());

        const auto decoded = decode<ByteVec>(*encoded);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->empty());
    }

    SECTION("int") {
        const auto encoded = encode<int>(-123456);
        REQUIRE(encoded.has_value());

        const auto decoded = decode<int>(*encoded);
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == -123456);
    }

    SECTION("double") {
        const auto encoded = encode<double>(std::numbers::pi);
        REQUIRE(encoded.has_value());

        const auto decoded = decode<double>(*encoded);
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == std::numbers::pi);
    }
}

TEST_CASE("decode rejects malformed input", "[serialization]") {
    SECTION("Buffer smaller than the length prefix") {
        const ByteVec buffer{std::byte{0x01}};

        const auto decoded = decode<std::string>(buffer);
        REQUIRE_FALSE(decoded.has_value());
    }

    SECTION("Declared length exceeds the buffer") {
        ByteVec buffer(sizeof(uint32_t) + 2, std::byte{0});
        const uint32_t declared = 10;
        std::memcpy(buffer.data(), &declared, sizeof(declared));

        const auto decoded = decode<std::string>(buffer);
        REQUIRE_FALSE(decoded.has_value());
    }

    SECTION("Trivially copyable type with mismatched length") {
        ByteVec buffer(sizeof(uint32_t) + sizeof(uint16_t), std::byte{0});
        const uint32_t declared = sizeof(uint16_t);
        std::memcpy(buffer.data(), &declared, sizeof(declared));

        const auto decoded = decode<uint32_t>(buffer);
        REQUIRE_FALSE(decoded.has_value());
    }
}
