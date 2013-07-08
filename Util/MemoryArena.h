#pragma once

#include <stdexcept>
#include <memory>
#include <functional>
#include <algorithm>
#include "MakeUnique.h"
 
namespace Wide {
    namespace Memory {
        class Arena {
        public:
            static const unsigned BufferSize = 20 * 1024; // Not chosen for any specific reason
        private:
            class MemoryBuffer {
                friend class Arena;
                Arena* mem_arena;
                std::unique_ptr<MemoryBuffer> next;
                char buffer[BufferSize];
                std::size_t usage;
                template<typename T> void* AllocateForNext() {
                    if (!next)
                        next = MakeUnique<MemoryBuffer>(mem_arena);
                    return next->AllocateFor<T>();
                }
                void* AllocateForNext(std::size_t size, std::size_t alignment) {
                    if (!next)
                        next = Memory::MakeUnique<MemoryBuffer>(mem_arena);
                    return next->AllocateFor(size, alignment);
                }
            public:
                MemoryBuffer(Arena* pointer)
                    : mem_arena(pointer)
                    , usage(0) {
                        pointer->tail = this;
                }
                void* AllocateFor(std::size_t size, std::size_t alignment) {
                    if (usage + size < BufferSize) {
                        if (usage % alignment != 0) {
                            auto alignment_increase = alignment - (usage % alignment);
                            if (usage + size + alignment_increase > BufferSize)
                                return AllocateForNext(size, alignment);
                            usage += alignment_increase;
                        }
                        auto ptr = &buffer[usage];
                        usage += size;
                        return ptr;
                    }
                    return AllocateForNext(size, alignment);
                }
                template<typename T> void* AllocateFor() {
                    static_assert(sizeof(T) < BufferSize, "Attempted to allocate an object too large.");
                    static const auto size = sizeof(T);
                    static const auto alignment = 8;//std::alignment_of<T>::value;
                    
                    if (usage + size < BufferSize) {
                        if (usage % alignment != 0) {
                            auto alignment_increase = alignment - (usage % alignment);
                            if (usage + size + alignment_increase > BufferSize)
                                return AllocateForNext<T>();
                            usage += alignment_increase;
                        }
                        char* ptr = &buffer[usage];
                        usage += size;
                        return ptr;
                    }
                    return AllocateForNext<T>();
                }
            };
            std::unique_ptr<MemoryBuffer> initial_buffer;
            MemoryBuffer* tail;
            void UpdateListForNewOwner() {
                MemoryBuffer* head = initial_buffer.get();
                while(head) {
                    head->mem_arena = this;
                    head = head->next.get();
                }
            }
        public:
            Arena(const Arena&);
            void empty() {
                MemoryBuffer* buf = initial_buffer.get();
                buf->usage = 0;
                // Explicitly silence warning- this is not a comparison.
                while((buf = buf->next.get())) {
                    buf->usage = 0;
                }
            }
            Arena() {
                initial_buffer = MakeUnique<MemoryBuffer>(this);
            }
            Arena(Arena&& other)
                : initial_buffer(std::move(other.initial_buffer))
                , tail(other.tail) 
            {
                UpdateListForNewOwner();
            }
            /*Arena(const Arena& other) {
            }*/
            Arena& operator=(Arena other) {
                this->swap(other);
                return *this;
            }
            void swap(Arena& other) {
                std::swap(initial_buffer, other.initial_buffer);
                std::swap(tail, other.tail);
                
                UpdateListForNewOwner();
                other.UpdateListForNewOwner();
            }
            void* Allocate(std::size_t size, std::size_t alignment = 16) {
                if (size > BufferSize)
                    throw std::runtime_error("Fuck");
                return tail->AllocateFor(size, alignment);
            }
            template<typename T> T* Allocate() {
                T* ret = new (tail->AllocateFor<T>()) T();
                return ret;
            }
            template<typename T, typename Arg1> T* Allocate(Arg1&& other) {
                auto mem = tail->AllocateFor<T>();
                T* ret = new (mem) T(std::forward<Arg1>(other));
                return ret;
            }
            template<typename T, typename Arg1, typename Arg2> T* Allocate(Arg1&& other, Arg2&& other2) {
                auto mem = tail->AllocateFor<T>();
                T* ret = new (mem) T(std::forward<Arg1>(other), std::forward<Arg2>(other2));
                return ret;
            }
            template<typename T, typename Arg1, typename Arg2, typename Arg3> T* Allocate(Arg1&& other, Arg2&& other2, Arg3&& other3) {
                auto mem = tail->AllocateFor<T>();
                T* ret = new (mem) T(std::forward<Arg1>(other), std::forward<Arg2>(other2), std::forward<Arg3>(other3));
                return ret;
            }
            template<typename T, typename Arg1, typename Arg2, typename Arg3, typename Arg4> T* Allocate(Arg1&& other, Arg2&& other2, Arg3&& other3, Arg4&& other4) {
                auto mem = tail->AllocateFor<T>();
                T* ret = new (mem) T(std::forward<Arg1>(other), std::forward<Arg2>(other2), std::forward<Arg3>(other3), std::forward<Arg4>(other4));
                return ret;
            }
            template<typename T, typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5> T* Allocate(Arg1&& other, Arg2&& other2, Arg3&& other3, Arg4&& other4, Arg5&& other5) {
                auto mem = tail->AllocateFor<T>();
                T* ret = new (mem) T(std::forward<Arg1>(other), std::forward<Arg2>(other2), std::forward<Arg3>(other3), std::forward<Arg4>(other4), std::forward<Arg5>(other5));
                return ret;
            }
            template<typename T, typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5, typename Arg6> T* Allocate(Arg1&& other, Arg2&& other2, Arg3&& other3, Arg4&& other4, Arg5&& other5, Arg6&& other6) {
                auto mem = tail->AllocateFor<T>();
                T* ret = new (mem) T(std::forward<Arg1>(other), std::forward<Arg2>(other2), std::forward<Arg3>(other3), std::forward<Arg4>(other4), std::forward<Arg5>(other5), std::forward<Arg6>(other6));
                return ret;
            }
            template<typename T, typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5, typename Arg6, typename Arg7> T* Allocate(Arg1&& other, Arg2&& other2, Arg3&& other3, Arg4&& other4, Arg5&& other5, Arg6&& other6, Arg7&& other7) {
                auto mem = tail->AllocateFor<T>();
                T* ret = new (mem) T(std::forward<Arg1>(other), std::forward<Arg2>(other2), std::forward<Arg3>(other3), std::forward<Arg4>(other4), std::forward<Arg5>(other5), std::forward<Arg6>(other6), std::forward<Arg7>(other7));
                return ret;
            }
        };
        template<typename T> struct ArenaAllocator {
            ArenaAllocator(Arena* ptr) {
                m = ptr;
            }
            template<typename Other> ArenaAllocator(const ArenaAllocator<Other>& other) {
                m = other.m;
            }
            template<typename Other> ArenaAllocator& operator=(const ArenaAllocator<Other>& other) {
                m = other.m;
            }
            Arena* m;
            typedef T* pointer;
            typedef T& reference;
            typedef const T* const_pointer;
            typedef const T& const_reference;
            typedef T value_type;
            typedef std::size_t size_type;
            typedef std::size_t difference_type;
 
            pointer address(reference val) const {
                return &val;
            }
            const_pointer address(const_reference val) const {
                return &val;
            }
            template<typename Other> struct rebind {
                typedef ArenaAllocator<Other> other;
            };
            pointer allocate(size_type count) {
                return reinterpret_cast<pointer>(m->Allocate(count * sizeof(T), std::alignment_of<T>::value));
            }
            template<typename Other> pointer allocate(size_type count, const Other* hint = 0) {
                return m->Allocate(count * sizeof(T), std::alignment_of<T>::value);
            }
            template<typename Arg> void construct(pointer p, Arg&& arg) {
                new (p) T(std::forward<Arg>(arg));
            }
            void deallocate(pointer p, size_type count) {
                // NOP
            }
            void destroy(pointer p) {
                p->T::~T();
            }
            size_type max_size() const {
                return Arena::BufferSize / sizeof(T);
            }
            bool operator==(const ArenaAllocator& other) const {
                return m == other.m;
            }
            bool operator!=(const ArenaAllocator& other) const {
                return m != other.m;
            }
        };
    }
}