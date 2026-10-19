#pragma once

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace Common {
/// Fixed-size object pool for hot-path allocations.
///
/// Design: a single contiguous vector of {T object_, bool is_free_} blocks
/// ("Design 2"). Keeping the flag right next to the object means checking a
/// slot and using its object touch the same cache line, unlike two parallel
/// vectors that live in different parts of memory.
///
/// All memory is reserved once in the constructor; allocate()/deallocate()
/// never touch the heap.
template <typename T>
class MemPool final {
public:
    /// Pre-allocate storage for num_elems objects, all marked free.
    explicit MemPool(std::size_t num_elems) : store_(num_elems, {T(), true}) {
        assert(num_elems > 0 && "MemPool needs at least one element");
        // deallocate() turns a T* back into an ObjectBlock*, which is only valid
        // if object_ is the first member at offset 0 of a standard-layout block.
        static_assert(std::is_standard_layout_v<ObjectBlock>);
        static_assert(offsetof(ObjectBlock, object_) == 0);
    }

    /// Construct a T in a free slot using the given constructor arguments and
    /// return a pointer to it. Pool must not be full.
    template <typename... Args>
    T *allocate(Args &&...args) noexcept {
        auto *block = &store_[next_free_index_];
        // next_free_index_ only points at a used block when every slot is taken.
        assert(block->is_free_ && "MemPool out of space");

        // Placement new: build the object in the pre-allocated slot, no heap call.
        T *ret = &block->object_;
        ret = new (ret) T(std::forward<Args>(args)...);
        block->is_free_ = false;

        updateNextFreeIndex();
        return ret;
    }

    /// Return an object previously handed out by allocate() back to the pool.
    void deallocate(const T *elem) noexcept {
        // object_ is at offset 0, so the T* is also the address of its block;
        // subtracting the vector's start gives the slot index.
        const auto elem_index = reinterpret_cast<const ObjectBlock *>(elem) - &store_[0];
        assert(elem_index >= 0 && static_cast<std::size_t>(elem_index) < store_.size() &&
               "element does not belong to this MemPool");
        assert(!store_[elem_index].is_free_ && "double free of MemPool element");

        store_[elem_index].is_free_ = true;

        // If the pool was full, next_free_index_ is parked on a used slot;
        // point it at the slot we just freed so allocate() can use it.
        if (!store_[next_free_index_].is_free_) [[unlikely]] {
            next_free_index_ = static_cast<std::size_t>(elem_index);
        }
    }

    // Deleted special members: the pool owns its backing storage and the free
    // list; copying would alias both.
    MemPool() = delete("a MemPool owns its backing storage and free list");
    MemPool(const MemPool &) = delete("a MemPool owns its backing storage and free list");
    MemPool(const MemPool &&) = delete("a MemPool owns its backing storage and free list");
    MemPool &operator=(const MemPool &) = delete("a MemPool owns its backing storage and free list");
    MemPool &operator=(const MemPool &&) = delete("a MemPool owns its backing storage and free list");

private:
    /// Scan forward (wrapping around) from the current index to the next free
    /// slot. If every slot is in use, leaves the index where it started.
    void updateNextFreeIndex() noexcept {
        const auto initial_free_index = next_free_index_;
        while (!store_[next_free_index_].is_free_) {
            ++next_free_index_;
            // Wrap back to the start instead of using %, which is a slow division.
            if (next_free_index_ == store_.size()) [[unlikely]] {
                next_free_index_ = 0;
            }
            // Came all the way around: pool is full. That's fine for now; the
            // next allocate() will catch it if it happens before a deallocate().
            if (next_free_index_ == initial_free_index) [[unlikely]] {
                return;
            }
        }
    }

    /// One slot: the object first (so T* == ObjectBlock*), then its free flag.
    struct ObjectBlock {
        T object_;
        bool is_free_ = true;
    };

    // Contiguous backing storage, sized once in the constructor and never resized.
    std::vector<ObjectBlock> store_;

    // Where the next allocate() will place its object.
    std::size_t next_free_index_ = 0;
};
}  // namespace Common
