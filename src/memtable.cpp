#include "storage_engine/memtable.hpp"

#include <map>

namespace {
struct ByteCompare {
    using is_transparent = void;

    static constexpr std::basic_string_view<std::byte> to_view(ByteSpan span) noexcept {
        return {span.data(), span.size()};
    }

    static constexpr std::basic_string_view<std::byte> to_view(const ByteVec& vec) noexcept {
        return {vec.data(), vec.size()};
    }

    template <typename T, typename U>
    bool operator()(const T& lhs, const U& rhs) const noexcept {
        return (to_view(lhs) <=> to_view(rhs)) < 0;
    }
};
} // namespace

// TODO: change this to a custom skiplist
struct Memtable::Impl {
    std::map<ByteVec, ByteVec, ByteCompare> data_;
};

Memtable::Memtable() : pimpl_(std::make_unique<Impl>()) {}
Memtable::~Memtable() = default;

void Memtable::put(const ByteVec& key, ByteVec value) noexcept {
    pimpl_->data_.insert_or_assign(key, std::move(value));
}

const ByteVec* Memtable::get(ByteSpan key) const noexcept {
    auto it = pimpl_->data_.find(key);
    if (it == pimpl_->data_.end()) {
        return nullptr;
    }
    return &(it->second);
}

ByteVec* Memtable::get(ByteSpan key) noexcept {
    return const_cast<ByteVec*>(std::as_const(*this).get(key)); // NOLINT
}

bool Memtable::write_to(std::ostream& stream) const noexcept {
    for (const auto& [key, value] : pimpl_->data_) {
        auto key_len = static_cast<uint32_t>(key.size());
        stream.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        stream.write(reinterpret_cast<const char*>(key.data()), key_len);

        auto val_len = static_cast<uint32_t>(value.size());
        stream.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        stream.write(reinterpret_cast<const char*>(value.data()), val_len);
    }
    return true;
}
