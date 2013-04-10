#pragma once

#include <mutex>
#include <deque>

namespace Wide {
    namespace Concurrency {
        template<typename T> class Queue {
            std::mutex m;
            std::deque<T> queue;
        public:
            Queue() {}
            Queue(const Queue&) { static_assert(false, "Concurrent queue cannot be guaranteed to be movable or copyable."); }
            Queue(Queue&&) { static_assert(false, "Concurrent queue cannot be guaranteed to be movable or copyable."); }
            template<typename Iterator> Queue(Iterator begin, Iterator end) : queue(begin, end) {}

            bool try_pop(T& t) {
                std::lock_guard<std::mutex> lock(m);
                if (queue.size() == 0)
                    return false;
                t = std::move(queue.front());
                queue.pop_front();
                return true;
            }
            void push(T t) {
                std::lock_guard<std::mutex> lock(m);
                queue.push_back(std::move(t));
            }
        };
    }
}