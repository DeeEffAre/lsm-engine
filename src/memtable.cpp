#include "storage_engine/memtable.hpp"

#include "storage_engine/errors.hpp"
#include "storage_engine/serialize_deserialize.hpp"
#include "storage_engine/types.hpp"

#include <algorithm>
#include <expected>
#include <ios>
#include <random>

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

class MemtableSkipList {
private:
    struct Node {
        RecordType record_type_;    // NOLINT
        ByteVec key;                // NOLINT
        ByteVec value;              // NOLINT
        std::vector<Node*> forward; // NOLINT

        Node(RecordType record_type, ByteVec k, ByteVec v, int level)
            : record_type_(record_type), key(std::move(k)), value(std::move(v)),
              forward(static_cast<size_t>(level), nullptr) {}
    };

    static constexpr int MAX_LEVEL = 16;
    static constexpr float P = 0.5F;

    Node* head_;
    int level_; // NOLINT
    ByteCompare compare_;

    static int random_level() noexcept {
        static thread_local std::mt19937 rng(std::random_device{}());
        static thread_local std::uniform_real_distribution<float> dist(0.0F, 1.0F);
        int lvl = 1;
        while (dist(rng) < P && lvl < MAX_LEVEL) {
            lvl++;
        }
        return lvl;
    }

    void clear_nodes() noexcept {
        Node* current = head_->forward[0];
        while (current != nullptr) {
            Node* next = current->forward[0];
            delete current; // NOLINT
            current = next;
        }
        std::ranges::fill(head_->forward, nullptr);
        level_ = 1;
    }

public:
    MemtableSkipList() : level_(1), compare_() {
        head_ = new Node(RecordType::PUT, {}, {}, MAX_LEVEL); // NOLINT
    }
    MemtableSkipList(const MemtableSkipList&) = default;
    MemtableSkipList(MemtableSkipList&&) = delete;
    MemtableSkipList& operator=(const MemtableSkipList&) = default;
    MemtableSkipList& operator=(MemtableSkipList&&) = delete;
    ~MemtableSkipList() {
        clear_nodes();
        delete head_;
    }

    void clear() noexcept { clear_nodes(); }

    void insert_or_assign(RecordType record_type, ByteVec key, ByteVec value) {
        std::vector<Node*> update(MAX_LEVEL, nullptr);
        Node* current = head_;

        for (int i = level_ - 1; i >= 0; --i) {
            while (current->forward[static_cast<size_t>(i)] != nullptr &&
                   compare_(current->forward[static_cast<size_t>(i)]->key, key)) {
                current = current->forward[static_cast<size_t>(i)];
            }
            update[static_cast<size_t>(i)] = current;
        }

        current = current->forward[0];

        // if key exists, assign the new value
        if (current != nullptr && !compare_(current->key, key) && !compare_(key, current->key)) {
            current->record_type_ = record_type;
            current->value = std::move(value);
            return;
        }

        const int new_level = random_level();
        if (new_level > level_) {
            for (int i = level_; i < new_level; ++i) {
                update[static_cast<size_t>(i)] = head_;
            }
            level_ = new_level;
        }

        // do not add value section when a record is delete
        Node* new_node; // NOLINT
        if (record_type == RecordType::DELETE) {
            new_node = new Node(record_type, std::move(key), {}, new_level); // NOLINT
        } else {
            new_node = new Node(record_type, std::move(key), std::move(value), new_level); // NOLINT
        }
        for (int i = 0; i < new_level; ++i) {
            new_node->forward[static_cast<size_t>(i)] =
                update[static_cast<size_t>(i)]->forward[static_cast<size_t>(i)];
            update[static_cast<size_t>(i)]->forward[static_cast<size_t>(i)] = new_node;
        }
    }

    // supports both ByteVec and ByteSpan as argument
    template <typename K>
    [[nodiscard]]
    std::expected<const ByteVec*, ErrorCode> find(const K& key) const noexcept {
        Node* current = head_;
        for (int i = level_ - 1; i >= 0; --i) {
            while (current->forward[static_cast<size_t>(i)] != nullptr &&
                   compare_(current->forward[static_cast<size_t>(i)]->key, key)) {
                current = current->forward[static_cast<size_t>(i)];
            }
        }
        current = current->forward[0];

        if (current == nullptr || compare_(current->key, key) || compare_(key, current->key)) {
            return std::unexpected(ErrorCode::NOT_FOUND);
        }

        if (current->record_type_ == RecordType::DELETE) {
            return std::unexpected(ErrorCode::KEY_DELETED);
        }

        return &(current->value);
    }

    // access to level-0
    [[nodiscard]]
    Node* begin() const noexcept {
        return head_->forward[0];
    }
};
} // namespace

struct Memtable::Impl {
    MemtableSkipList data_; // NOLINT

    void clear() noexcept { data_.clear(); }
};

Memtable::Memtable() : pimpl_(std::make_unique<Impl>()) {}
Memtable::~Memtable() = default;

// Because we use pimpl pattern we cannot declare how to move Memtable class in .hpp
// So we only declare that `move constructors are present but the implementations in .cpp` in .hpp
// If we try to declare the impl in .hpp we can't know the size of pimpl_ and because of that
// compiler gives an error
Memtable::Memtable(Memtable&&) noexcept = default;
Memtable& Memtable::operator=(Memtable&&) noexcept = default;

void Memtable::put(RecordType record_type, ByteVec key, ByteVec value) noexcept {
    pimpl_->data_.insert_or_assign(record_type, std::move(key), std::move(value));
}

auto Memtable::get(ByteSpan key) const noexcept -> std::expected<const ByteVec*, ErrorCode> {
    return pimpl_->data_.find(key);
}

auto Memtable::get(ByteSpan key) noexcept -> std::expected<ByteVec*, ErrorCode> {
    return std::as_const(*this).get(key).transform(
        [](auto inner) { return const_cast<ByteVec*>(inner); });
}

bool Memtable::write_to(std::ostream& stream) const noexcept {
    for (auto* node = pimpl_->data_.begin(); node != nullptr; node = node->forward[0]) {
        auto record_type = node->record_type_;
        auto encoded_key = encode<ByteVec>(node->key);
        auto encoded_value = encode<ByteVec>(node->value);
        if (!encoded_key.has_value() || !encoded_value.has_value()) {
            return false;
        }

        // don't add the value section if it is a DELETE record
        if (record_type == RecordType::DELETE) {
            stream.write(reinterpret_cast<const char*>(&record_type),
                         static_cast<std::streamsize>(sizeof(record_type)));
            stream.write(reinterpret_cast<const char*>(encoded_key->data()),
                         static_cast<std::streamsize>(encoded_key->size()));
        }

        if (record_type == RecordType::PUT) {
            stream.write(reinterpret_cast<const char*>(&record_type),
                         static_cast<std::streamsize>(sizeof(record_type)));
            stream.write(reinterpret_cast<const char*>(encoded_key->data()),
                         static_cast<std::streamsize>(encoded_key->size()));
            stream.write(reinterpret_cast<const char*>(encoded_value->data()),
                         static_cast<std::streamsize>(encoded_value->size()));
        }
    }

    return static_cast<bool>(stream);
}

void Memtable::clear() noexcept {
    pimpl_->clear();
}
