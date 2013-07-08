#pragma once

#include <samples/concurrent_unordered_map.h>

namespace Wide {
    namespace Concurrency {
        template<typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>> class UnorderedMap {
            ::Concurrency::samples::concurrent_unordered_map<K, V, H, E> umap;
        public:
            std::pair<typename ::Concurrency::samples::concurrent_unordered_map<K, V, H, E>::iterator, bool> insert(std::pair<const K, V> val) {
                return umap.insert(std::move(val));
            }
            
            iterator find(K k) {
                return umap.find(k);
            }
            iterator end() {
                return umap.end();
            }
            V& operator[](K k) {
                return umap[k];
            }
        };
    }
}