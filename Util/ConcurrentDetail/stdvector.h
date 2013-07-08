#pragma once

#include <vector>
#include <mutex>

namespace Wide {
    namespace Concurrency {
        template<typename T> class Vector {
            std::vector<T> vec;
            std::mutex m;
        public:
            void push_back(T t) {
                std::lock_guard<std::mutex> lock(m);
                vec.push_back(std::move(t));
            }

            Vector& operator=(const Vector& other) {
                vec = other.vec;
            }

            // Not concurrency safe:
            typename std::vector<T>::iterator begin() {
                return vec.begin();
            }
            typename std::vector<T>::iterator end() {
                return vec.end();
            }
            bool empty() {
                return vec.empty();
            }
        };
    }
}