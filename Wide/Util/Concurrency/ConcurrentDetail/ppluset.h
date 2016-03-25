#pragma once

#include <concurrent_unordered_set.h>

namespace Wide {
    namespace Concurrency {
        template<typename T, typename H = std::hash<T>, typename E = std::equal_to<T>> class UnorderedSet {
            ::Concurrency::concurrent_unordered_set<T, H, E> umap;
        public:
            typedef typename ::Concurrency::concurrent_unordered_set<T, H, E>::iterator iterator;
            std::pair<iterator, bool> insert(T val) {
                return umap.insert(std::move(val));
            }
            iterator begin() {
                return umap.begin();
            }
            iterator find(T k) {
                return umap.find(k);
            }
            iterator end() {
                return umap.end();
            }
            std::size_t size() {
                return umap.size();
            }
            bool empty() {
                return umap.empty();
            }
        };
    }
}