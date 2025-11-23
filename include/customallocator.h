#pragma once

#include <cstddef>
#include <memory>
#include <vector>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <iostream>
#include <map>
#include <string>
#include <limits>

template <typename T>
struct PoolState {
    using size_type = std::size_t;

    explicit PoolState(size_type chunk_elems)
        : chunk_elems(chunk_elems),
          element_size(sizeof(T)),
          current_block_index(0),
          current_offset(0)
    {
        if (chunk_elems == 0) {
            throw std::invalid_argument("chunk_elems must be positive");
        }
        // Сразу создаем первый блок
        add_block(chunk_elems);
    }

    ~PoolState() {
        release_all_blocks();
    }

    PoolState(const PoolState&) = delete;
    PoolState& operator=(const PoolState&) = delete;

    // Добавить новый блок
    void add_block(size_type elems) {
        if (elems == 0) return;
        
        size_type bytes = elems * element_size;
        void* raw = ::operator new(bytes, std::nothrow);
        if (!raw) {
            throw std::bad_alloc();
        }
        blocks.push_back(raw);
        block_elems.push_back(elems);
        current_block_index = blocks.size() - 1;
        current_offset = 0;
    }

    bool current_block_has(size_type n) const noexcept {
        if (blocks.empty()) return false;
        return current_offset + n <= block_elems[current_block_index];
    }

    void* alloc_from_current(size_type n) {
        if (!current_block_has(n)) {
            throw std::bad_alloc();
        }
        char* base = static_cast<char*>(blocks[current_block_index]);
        void* ptr = base + current_offset * element_size;
        current_offset += n;
        return ptr;
    }

    void reserve_elements(size_type total_elems) {
        if (total_elems == 0) return;
        
        size_type available = 0;
        for (size_type i = 0; i < blocks.size(); ++i) {
            if (i == current_block_index) {
                available += (block_elems[i] - current_offset);
            } else {
                available += block_elems[i];
            }
        }
        if (available >= total_elems) return;

        size_type need = total_elems - available;
        while (need > 0) {
            size_type allocate_elems = std::max(chunk_elems, need);
            add_block(allocate_elems);
            if (need <= allocate_elems) break;
            need -= allocate_elems;
        }
    }

    void release_all_blocks() noexcept {
        for (void* p : blocks) {
            ::operator delete(p);
        }
        blocks.clear();
        block_elems.clear();
        current_block_index = 0;
        current_offset = 0;
        free_list.clear();
    }
    
    void push_free(void* p) noexcept { 
        free_list.push_back(p); 
    }
    
    void* pop_free() noexcept {
        if (free_list.empty()) return nullptr;
        void* p = free_list.back();
        free_list.pop_back();
        return p;
    }


    std::vector<void*> blocks;
    std::vector<size_type> block_elems;

    const size_type chunk_elems;
    const size_type element_size;

    size_type current_block_index;
    size_type current_offset;

    std::vector<void*> free_list;
};


template <typename T>
class PoolHandle {
public:
    using size_type = std::size_t;

    explicit PoolHandle(size_type chunk_elems)
        : state_(std::make_shared<PoolState<T>>(chunk_elems)) {}

    std::shared_ptr<PoolState<T>> get_state() const noexcept {
        return state_;
    }

    bool valid() const noexcept {
        return state_ != nullptr;
    }

    void reset() noexcept {
        state_.reset();
    }

private:
    std::shared_ptr<PoolState<T>> state_;
};


template <typename T,
          std::size_t ChunkElems = 10,
          bool Expandable = true,
          bool PerElementFree = false>
class CustomAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;

    using is_always_equal = std::false_type;
    using propagate_on_container_copy_assignment  = std::true_type;
    using propagate_on_container_move_assignment  = std::true_type;
    using propagate_on_container_swap             = std::true_type;

    template <typename U>
    struct rebind {
        using other = CustomAllocator<U, ChunkElems, Expandable, PerElementFree>;
    };

private:
    std::shared_ptr<PoolHandle<T>> handle_;

    std::shared_ptr<PoolState<T>> get_state() const noexcept {
        if (handle_ && handle_->valid()) {
            return handle_->get_state();
        }
        return nullptr;
    }

public:
    CustomAllocator() noexcept
        : handle_(std::make_shared<PoolHandle<T>>(ChunkElems)) {}

    template <typename U>
    explicit CustomAllocator(const CustomAllocator<U, ChunkElems, Expandable, PerElementFree>& other) noexcept
    {
        if (auto other_handle = other.get_handle()) {

            handle_ = std::make_shared<PoolHandle<T>>(ChunkElems);
        }
    }

    CustomAllocator(const CustomAllocator&) noexcept = default;
    CustomAllocator(CustomAllocator&&) noexcept = default;

    CustomAllocator& operator=(const CustomAllocator&) noexcept = default;
    CustomAllocator& operator=(CustomAllocator&&) noexcept = default;

    [[nodiscard]] pointer allocate(size_type n) {
        auto state = get_state();
        if (!state) throw std::bad_alloc();
        if (n == 0) return nullptr;

        if (n > max_size()) {
            throw std::bad_array_new_length();
        }

        if (!Expandable && n > state->chunk_elems) {
            throw std::bad_alloc();
        }

        if constexpr (PerElementFree) {
            if (n == 1) {
                void* p = state->pop_free();
                if (p) return static_cast<pointer>(p);
            }
        }

        if (state->current_block_has(n)) {
            return static_cast<pointer>(state->alloc_from_current(n));
        }

        if (!Expandable) {
            throw std::bad_alloc();
        }

        size_type want = std::max(n, state->chunk_elems);
        state->add_block(want);
        return static_cast<pointer>(state->alloc_from_current(n));
    }

    void deallocate(pointer p, size_type n) noexcept {
        if (!p) return;
        
        auto state = get_state();
        if (!state) return;

        if constexpr (PerElementFree) {
            if (n == 1) {
                state->push_free(p);
                return;
            }
        }
    }

    size_type max_size() const noexcept {
        return std::numeric_limits<size_type>::max() / sizeof(T);
    }

    void reserve_elements(size_type count) {
        if (auto state = get_state()) {
            state->reserve_elements(count);
        }
    }

    std::shared_ptr<PoolHandle<T>> get_handle() const noexcept {
        return handle_;
    }

    template <typename U, std::size_t C2, bool E2, bool P2>
    bool operator==(const CustomAllocator<U, C2, E2, P2>& other) const noexcept {
        return get_state().get() == other.get_state().get();
    }

    template <typename U, std::size_t C2, bool E2, bool P2>
    bool operator!=(const CustomAllocator<U, C2, E2, P2>& other) const noexcept {
        return !(*this == other);
    }

private:
    template<typename U, std::size_t C, bool E, bool P>
    friend class CustomAllocator;
};