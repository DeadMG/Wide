#pragma once

#include <mutex>
#include <unordered_map>

namespace Wide {
    namespace Concurrency {
        template<typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>> class UnorderedMap {
            std::mutex m;
            std::unordered_map<K, V, H, E> umap;
        public:
            typedef typename std::unordered_map<K, V, H, E>::iterator iterator;

            std::pair<iterator, bool> insert(std::pair<const K, V> val) {
                std::lock_guard<std::mutex> lock(m);
                return umap.insert(std::move(val));
            }

            // Not concurrency safe!
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