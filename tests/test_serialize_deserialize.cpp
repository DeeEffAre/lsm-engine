#include "storage_engine/serialize_deserialize.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

template <typename T>
ByteVec serialized(const T& value) {
    auto encoded = serialize_value(value);
    REQUIRE(encoded.has_value());
    return *encoded;
}

template <typename T>
ByteSpan serialized_view(const T& value) {
    auto view = serialize_view(value);
    REQUIRE(view.has_value());
    return *view;
}

template <typename T>
view_of_t<T> deserialized_view(const ByteVec& buffer) {
    auto view = deserialize_view<T>(buffer);
    REQUIRE(view.has_value());
    return *view;
}

} // namespace

TEST_CASE("Serialization round-trips supported types", "[serialization]") {
    SECTION("std::string") {
        const std::string value = "hello world";
        const ByteVec encoded = serialized(value);

        REQUIRE(encoded.size() == value.size());

        const auto encoded_size = get_encoded_size(value);
        REQUIRE(encoded_size.has_value());
        REQUIRE(*encoded_size == encoded.size());

        const auto decoded = deserialize_value<std::string>(encoded, *encoded_size);
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == value);
    }

    SECTION("empty std::string") {
        const std::string value;
        const ByteVec encoded = serialized(value);

        REQUIRE(encoded.empty());

        const auto encoded_size = get_encoded_size(value);
        REQUIRE(encoded_size.has_value());
        REQUIRE(*encoded_size == 0);

        const auto decoded = deserialize_value<std::string>(encoded, 0);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->empty());
    }

    SECTION("std::string with embedded null bytes") {
        const std::string value("a\0b", 3);
        const ByteVec encoded = serialized(value);

        REQUIRE(encoded.size() == value.size());

        const auto decoded = deserialize_value<std::string>(encoded, *get_encoded_size(value));
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == value);
    }

    SECTION("int") {
        const int value = -123456;
        const ByteVec encoded = serialized(value);

        REQUIRE(encoded.size() == sizeof(int));

        const auto encoded_size = get_encoded_size(value);
        REQUIRE(encoded_size.has_value());
        REQUIRE(*encoded_size == sizeof(int));

        const auto decoded = deserialize_value<int>(encoded, *encoded_size);
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == value);
    }

    SECTION("uint32_t") {
        const uint32_t value = 0xDEADBEEF;
        const ByteVec encoded = serialized(value);

        REQUIRE(encoded.size() == sizeof(uint32_t));

        const auto encoded_size = get_encoded_size(value);
        REQUIRE(encoded_size.has_value());
        REQUIRE(*encoded_size == sizeof(uint32_t));

        const auto decoded = deserialize_value<uint32_t>(encoded, *encoded_size);
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == value);
    }

    SECTION("double") {
        const double value = std::numbers::pi;
        const ByteVec encoded = serialized(value);

        REQUIRE(encoded.size() == sizeof(double));

        const auto encoded_size = get_encoded_size(value);
        REQUIRE(encoded_size.has_value());
        REQUIRE(*encoded_size == sizeof(double));

        const auto decoded = deserialize_value<double>(encoded, *encoded_size);
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == value);
    }
}

TEST_CASE("serialize_view is a zero-copy view of the value", "[serialization]") {
    SECTION("std::string") {
        const std::string value = "view me";
        const ByteSpan view = serialized_view(value);

        REQUIRE(view.size() == value.size());
        REQUIRE(view.data() == reinterpret_cast<const std::byte*>(value.data()));
        REQUIRE(std::ranges::equal(view, serialized(value)));
    }

    SECTION("trivially copyable type") {
        const uint64_t value = 42;
        const ByteSpan view = serialized_view(value);

        REQUIRE(view.size() == sizeof(uint64_t));
        REQUIRE(view.data() == reinterpret_cast<const std::byte*>(&value));
        REQUIRE(std::ranges::equal(view, serialized(value)));
    }
}

TEST_CASE("deserialize_view returns views into the source buffer", "[serialization]") {
    SECTION("std::string view points at the buffer without copying") {
        const std::string original = "hello view";
        const ByteVec buffer = serialized(original);

        const std::string_view view = deserialized_view<std::string>(buffer);

        REQUIRE(view.data() == reinterpret_cast<const char*>(buffer.data()));
        REQUIRE(view.size() == buffer.size());
        REQUIRE(view == original);
    }

    SECTION("std::string view observes in-place mutations of the buffer") {
        ByteVec buffer = serialized<std::string>("hello");

        const std::string_view view = deserialized_view<std::string>(buffer);
        buffer[0] = static_cast<std::byte>('H');

        REQUIRE(view == "Hello");
    }

    SECTION("std::string view of an empty buffer is empty") {
        const ByteVec buffer = serialized<std::string>("");

        const std::string_view view = deserialized_view<std::string>(buffer);

        REQUIRE(view.empty());
        REQUIRE(view.data() == reinterpret_cast<const char*>(buffer.data()));
    }

    SECTION("ByteSpan overload views only the given span") {
        const ByteVec buffer = serialized<std::string>("prefix-view");
        const ByteSpan span = ByteSpan{buffer}.subspan(7);

        const auto view_expected = deserialize_view<std::string>(span);
        REQUIRE(view_expected.has_value());
        const std::string_view view = *view_expected;

        REQUIRE(view.data() == reinterpret_cast<const char*>(buffer.data() + 7));
        REQUIRE(view.size() == 4);
        REQUIRE(view == "view");
    }

    SECTION("trivially copyable view is a pointer into the buffer") {
        const uint32_t original = 0xDEADBEEF;
        const ByteVec buffer = serialized(original);

        const auto* view = deserialized_view<uint32_t>(buffer);

        REQUIRE(view == reinterpret_cast<const uint32_t*>(buffer.data()));
        REQUIRE(*view == original);
    }

    SECTION("trivially copyable view observes in-place mutations of the buffer") {
        ByteVec buffer = serialized<uint32_t>(0x11223344);

        const auto* view = deserialized_view<uint32_t>(buffer);
        const uint32_t new_value = 0x55667788;
        std::memcpy(buffer.data(), &new_value, sizeof(new_value));

        REQUIRE(*view == new_value);
    }

    SECTION("trivially copyable view fails when the buffer is too small") {
        const ByteVec buffer{std::byte{0x01}};

        const auto view = deserialize_view<uint32_t>(buffer);
        REQUIRE_FALSE(view.has_value());
    }
}

TEST_CASE("deserialize_view types are chosen at compile time", "[serialization]") {
    STATIC_REQUIRE(std::is_same_v<view_of_t<std::string>, std::string_view>);
    STATIC_REQUIRE(std::is_same_v<view_of_t<int>, const int*>);
}

TEST_CASE("deserialize_view does not track the lifetime of its buffer", "[serialization]") {
    SECTION("Reallocation moves the buffer but not the view") {
        ByteVec buffer = serialized<std::string>("do not outlive me");
        const std::string_view view = deserialized_view<std::string>(buffer);

        const char* const old_address = view.data();
        buffer.reserve(buffer.capacity() + 1);

        // The view object still stores the old address, but the buffer has moved
        // to new storage: the view is now dangling and must not be dereferenced.
        REQUIRE(view.data() == old_address);
        REQUIRE(reinterpret_cast<const char*>(buffer.data()) != old_address);

        // The only safe option is to take a fresh view from the live buffer.
        const auto fresh = deserialize_view<std::string>(buffer);
        REQUIRE(fresh.has_value());
        REQUIRE(*fresh == "do not outlive me");
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
        const ByteVec buffer = serialized<std::string>("dangling view");
        dangling = deserialized_view<std::string>(buffer);
    }

    // `buffer` is gone; the following reads touch freed memory.
    REQUIRE(dangling.size() == 13);
    REQUIRE(dangling == "dangling view");
}
#endif

TEST_CASE("encode prefixes the payload with a LEB128 length", "[serialization]") {
    SECTION("std::string") {
        const std::string value = "hello";
        const auto encoded = encode(value);
        REQUIRE(encoded.has_value());

        const auto length = decode_leb128(*encoded);
        REQUIRE(length.has_value());
        REQUIRE(length->value == value.size());
        REQUIRE(encoded->size() == length->bytes_read + value.size());
        REQUIRE(std::memcmp(encoded->data() + length->bytes_read, value.data(), value.size()) == 0);
    }

    SECTION("ByteVec") {
        const ByteVec value = serialized<std::string>("bytes");
        const auto encoded = encode<ByteVec>(value);
        REQUIRE(encoded.has_value());

        const auto length = decode_leb128(*encoded);
        REQUIRE(length.has_value());
        REQUIRE(length->value == value.size());
        REQUIRE(encoded->size() == length->bytes_read + value.size());
    }
}

TEST_CASE("LEB128 length coding round-trips", "[serialization]") {
    for (const uint32_t value : {0U, 1U, 127U, 128U, 16383U, 16384U, 0xFFFFFFFFU}) {
        const auto encoded = encode_leb128(value);
        REQUIRE(encoded.size >= 1);
        REQUIRE(encoded.size <= 5);

        const auto decoded = decode_leb128(ByteSpan{encoded.data}.first(encoded.size));
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->value == value);
        REQUIRE(decoded->bytes_read == encoded.size);
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
        REQUIRE(encoded->size() == 1); // single-byte LEB128 prefix for length 0

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
        const ByteVec value = serialized<std::string>("raw bytes");
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
