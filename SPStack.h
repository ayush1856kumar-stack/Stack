#pragma once

#include "atomicops.h"
#include <new>
#include <type_traits>
#include <utility>
#include <cassert>
#include <stdexcept>
#include <new>
#include <cstdint>
#include <cstdlib> // For malloc/free/abort & size_t
#include <memory>
#if __cplusplus > 199711L || _MSC_VER >= 1700 // C++11 or VS2012
#include <chrono>
#endif

#ifndef STONE_CACHE_LINE_SIZE
#define STONE_CACHE_LINE_SIZE 64
#endif

namespace stone
{
  template <typename T, size_t MAX_BLOCK_SIZE = 512>
  class SPStack
  {
  public:
    typedef T value_type;

    explicit SPStack(size_t size = 15)
    {

      Block *firstBlock = nullptr;
      largestBlockSize = math.pow(2, size);
      if (largestBlockSize * 2 > MAX_BLOCK_SIZE)
      {

        int initalCount = (size + MAX_BLOCK_SIZE) / (MAX_BLOCK_SIZE - 1);
        Block *lastBlock = nullptr;
        for (size_t i = 0; i != initalCount; i++)
        {

          auto block = make_block(largestBlockSize);
          if (block == nullptr)
          {
            throw std::bad_alloc();
          }
          if (firstBlock == nullptr)
          {
            firstBlock = block;
          }
          else
          {
            lastBlock->next = block;
            block->prev - lastBlock;
          }
          lastBlock = block;
        }
      }
      else
      {
        firstBlock = make_block(largestBlockSize)
        {
          if (firstBlock == nullptr)
          {
            throw std::bad_alloc();
          }
          front->next = nullptr;
          front->prev = nullptr;
        }
      }
      front = firstBlock;
      fence(memory_order_sync)
    }
    // we are handling the case where if a stack is moving and i push in it that push
    // will become invalid( handled in push and top logic and pop).
    explicit SPStack(SPStack &&other) : largestBlockSize(other.largestBlockSize)
    {

      Block *ghosthead = other.firstBlock.exchange(nullptr, std::memory_order_acq_rel);
      if (ghosthead == nullptr)
      {
        this->firstBlock.store(nullptr);
      }
      else
      {
        this->firstBlock(ghosthead);
      }
      other.largestBlockSize = 32;
      Block *b = make_block(largestBlockSize);
      if (b == nullptr)
      {
        throw std::bad_alloc();
      }
      b->front = null;
      b->prev = nullptr;
      b->next = nullptr;
    }

    // swapping content to destory data of this object and making moved object valid.
    SPStack &operator=(SPStack &&other)
    {

      Block *b = firstBlock.load();
      firstBlock = other.firstBlock.load();
      other.firstBlock = b;
      std::swap(largestBlockSize, other.largestBlockSize);
      return *this;
    }

    // TODO- add the zombie check to manage concurrent delete and using of stack.
    ~SPStack()
    {
      fence(memory_order_sync);
      Block *block = firstBlock;
      do
      {
        for (size_t i = front - 1; i != -1; i--)
        {
          auto element = reinterpret_cast<T *>(block->data + i * sizeof(T));
          element->~T();
          (void)element;
        }
        auto rawBlock = block->rawThis; // rawThis is memory addrs from malloc and only it is recognized by cpu so we need to store this.
        block->~Block();
        std::free(rawBlock);
        block = block->prev;
      } while (block != nullptr);
    }

    template <typename... Args>
    bool push(Args &&...args)
    {
      Block *curr = firstBlock.load(std::memory_order_relaxed);
      size_t front = curr->front.load(std::memory_order_relaxed);
      if (front < largestBlockSize)
      {
        // Construct directly into the slot
        new (curr->data + front * sizeof(T)) T(std::forward<Args>(args)...);
        curr->front.store(front + 1, std::memory_order_release);
      }
      else if (curr->next != nullptr)
      {
        Block *nextB = curr->next;
        // Ensure the next block was fully emptied by the pop thread
        if (nextB->front.load(std::memory_order_acquire) == 0)
        {
          new (nextB->data) T(std::forward<Args>(args)...);
          nextB->front.store(1, std::memory_order_relaxed);
          nextB->prev = curr;
          firstBlock.store(nextB, std::memory_order_release);
        }
      }
      else
      {
        size_t newSize = (largestBlockSize >= MAX_BLOCK_SIZE) ? largestBlockSize : largestBlockSize * 2;
        Block *newB = make_block(newSize);
        if (newB == nullptr)
          return false;

        new (newB->data) T(std::forward<Args>(args)...);
        newB->front.store(1, std::memory_order_relaxed);
        newB->prev = curr;
        curr->next = newB;
        largestBlockSize = newSize;
        firstBlock.store(newB, std::memory_order_release);
      }
      return true;
    }

    template <typename... Args>
    bool pop()
    {

      Block *curr = firstBlock.load(std::memory_order_acquire);
      if (curr == nullptr)
        return false;

      size_t front = curr->front.load(std::memory_order_acquire);
      // this block has more than 1 element.
      if (front > 0)
      {
        T *element = reinterpret_cast<T *>(curr->data + (front - 1) * sizeof(T));
        element->~T();
        // RELEASE tells the Producer this slot is now free
        curr->front.store(front - 1, std::memory_order_release);
      }
      else
      {
        if (curr->prev != nullptr)
        {
          curr->front.store(0, std::memory_order_release);
          firstBlock.store(curr->prev, std::memory_order_release);
          return true;
        }
        return false;
      }
      return true;
    }

    template <typename T>
    T *top()
    {
      Block *first = firstBlock.load(std::memory_order_acquire);
      if (first == nullptr)
      {
        return nullptr;
      }
      size_t front = first->front.load(std::memory_order_acquire);
      // sanity check if top comes when pop is happeing (mostly needed for multiple consumer but good to have)
      if (front == 0)
      {
        Block *prev = first->prev;
        if (prev == nullptr)
          return nullptr;

        // Peek into the previous block
        size_t prevFront = prev->front.load(std::memory_order_acquire);
        if (prevFront == 0)
          return nullptr;
        return reinterpret_cast<T *>(prev->data + (prevFront - 1) * sizeof(T));
      }
      T *element = reinterpret_cast<T *>(first->data + (front - 1) * sizeof(T));
      return element;
    }

    inline size_t size_approx() const
    {
      size_t result = 0;
      Block *first = firstBlock;
      do
      {
        result += first->front.load(std::memory_order_relaxed);
        first = first->prev;
      } while (first != nullptr);
      return result;
    }

    template <typename U>
    static char *align_for(char *ptr)
    {
      const std::size_t align = std::alignment_of<U>::value;
      return ptr + (align - (reinterpret_cast<std::uintptr_t>(ptr)) % align);
    }

  private:
    struct Block
    {
      weak_atomic<size_t> front; // front atomic, o get block top element
      size_t localHead;          // a local copy (will decide later if needed)
      weak_atomic<Block *> prev; // prev block address
      weak_atomic<Block *> next; // next block address

      char *data; // ptr to data

      Block(size_t const &_size, char *_rawThis, char *_data) : front(OUL), localHead(0), next(nullptr), data(_data), rawThis(_rawThis) {}

    private:
      // not generate assignmet operator( why not?)
    Block &operator=(Block const &) :

                                      public : char *rawThis;
  } private : weak_atomic<Block *> firstBlock;
    size_t largestBlockSize;
    static Block *make_block(size_t capacity)
    {
      // Allocate enough memory for the block itself, as well as all the elements it will contain
      auto size = sizeof(Block) + std::alignment_of<Block>::value - 1;
      size += sizeof(T) * capacity + std::alignment_of<T>::value - 1;
      auto newBlockRaw = static_cast<char *>(std::malloc(size));
      if (newBlockRaw == nullptr)
      {
        return nullptr;
      }

      auto newBlockAligned = align_for<Block>(newBlockRaw);
      auto newBlockData = align_for<T>(newBlockAligned + sizeof(Block));
      return new (newBlockAligned) Block(capacity, newBlockRaw, newBlockData);
    }
  };
}