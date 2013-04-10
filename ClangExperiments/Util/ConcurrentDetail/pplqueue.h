#pragma once

#include <concurrent_queue.h>

namespace Wide {
    namespace Concurrency {
        template<typename T> class Queue {
            ::Concurrency::concurrent_queue<T> queue;
            Queue(const Queue&) { static_assert(false, "Concurrent queue cannot be guaranteed to be movable or copyable."); }
        public:
            Queue() {}
            Queue(Queue&&) { static_assert(false, "Concurrent queue cannot be guaranteed to be movable or copyable."); }
            template<typename Iterator> Queue(Iterator begin, Iterator end) : queue(begin, end) {}

            bool try_pop(T& t) {
                return queue.try_pop(t);
            }
            void push(T t) {
                queue.push(std::move(t));
            }
        };
    }
}