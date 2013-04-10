#pragma once

#include <concurrent_vector.h>

namespace Wide {
    namespace Concurrency {
        template<typename T> class Vector {
            ::Concurrency::concurrent_vector<T> vec;
        public:
            void push_back(T t) {
                vec.push_back(std::move(t));
            }
            typename ::Concurrency::concurrent_vector<T>::iterator begin() {
                return vec.begin();
            }
            typename ::Concurrency::concurrent_vector<T>::iterator end() {
                return vec.end();
            }
        };
    }
}