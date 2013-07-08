#pragma once

#include <concurrent_unordered_map.h>

namespace Wide {
    namespace Concurrency {
        template<typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>> class UnorderedMap {
            ::Concurrency::concurrent_unordered_map<K, V, H, E> umap;
        public:
            typedef typename ::Concurrency::concurrent_unordered_map<K, V, H, E>::iterator iterator;
            std::pair<iterator, bool> insert(std::pair<const K, V> val) {
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