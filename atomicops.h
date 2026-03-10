#pragma once

// Provides portable (VC++2010+, Intel ICC 13, GCC 4.7+, and anything C++11 compliant) implementation
// of low-level memory barriers, plus a few semi-portable utility macros (for inlining and alignment).
// Also has a basic atomic type (limited to hardware-supported atomics with no memory ordering guarantees).
// Uses the AE_* prefix for macros (historical reasons), and the "moodycamel" namespace for symbols.

#include <cerrno>
#include <cassert>
#include <type_traits>
#include <cstdint>
#include <ctime>
#include<atomic>    
#include <utility>
namespace stone
{
    template <typename T>
    class weak_atomic
    {
    public:
        weak_atomic() : value() {}
        template <typename U>
        weak_atomic(U &&x) : value(std::forward<U>(x)) {}
        weak_atomic(weak_atomic const &other) : value(other.load()) {}
        weak_atomic(weak_atomic &&other) : value(std::move(other.load())) {}
        operator T()
        {
            return load();
        }
        template <typename U>
        weak_atomic const &operator=(U &&x)
        {
            value.store(std::forward<U>(x), std::memory_order_relaxed);
            return *this;
        }

        weak_atomic const &operator=(weak_atomic const &other)
        {
            value.store(other.value.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return *this;
        }

        // to avoid reordering but the stale value might not be a problem as this is only 
        // being used in loop.
        T load() const { return value.load(std::memory_order_relaxed); }
        T load(std::memory_order order) const { return value.load(order); }

        void store(T val, std::memory_order order = std::memory_order_relaxed)
        {
            value.store(val, order);
        }

        // this telll cpu to first push all array content in stack then only incremnet this var 
        // basiclally flushing the STORE BUFFER.
        T fetch_add_acquire(T increment)
        {
            return value.fetch_add(increment,std::memory_order_acquire);
        }

        // similar to this first finish all LOAD BUFFER then only update this value.
        T fetch_add_release(T increment)
        {
            return value.fetch_add(increment, std::memory_order_release);
        }

    private:
        std::atomic<T> value;
    };
}