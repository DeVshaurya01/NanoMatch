// Brick 6 — Slab object pool. Pre-allocated array of fixed-size slots with a
// free list threaded through unused slots. Allocate/free are O(1) and no heap
// is touched after construction.
#pragma once

#include "types.hpp"
#include <array>
#include <memory>

namespace nanomatch {

template <typename T, std::uint32_t Capacity>
class ObjectPool {
public:
    static constexpr std::uint32_t kCapacity = Capacity;

    ObjectPool()
        : storage_(std::make_unique<Slot[]>(Capacity))
        , free_head_(0) {
        for (std::uint32_t i = 0; i < Capacity - 1; ++i) {
            storage_[i].next = i + 1;
        }
        storage_[Capacity - 1].next = kNullIdx;
    }

    PoolIdx alloc() noexcept {
        const PoolIdx idx = free_head_;
        if (idx == kNullIdx) [[unlikely]] return kNullIdx;
        free_head_ = storage_[idx].next;
        return idx;
    }

    void free(PoolIdx idx) noexcept {
        storage_[idx].next = free_head_;
        free_head_ = idx;
    }

    T&       operator[](PoolIdx i) noexcept       { return storage_[i].value; }
    const T& operator[](PoolIdx i) const noexcept { return storage_[i].value; }

private:
    // Union the user data with the free-list link so unused slots cost nothing.
    union Slot {
        T          value;
        PoolIdx    next;
        Slot() : next(kNullIdx) {}
        ~Slot() {}
    };

    std::unique_ptr<Slot[]> storage_;
    PoolIdx                 free_head_;
};

} // namespace nanomatch
