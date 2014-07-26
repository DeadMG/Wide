#pragma once

#include <mutex>
#include <unordered_set>

namespace Wide {
    namespace Concurrency {
        template<typename T, typename H = std::hash<T>, typename E = std::equal_to<T>> class UnorderedMap {
            std::mutex m;
            std::unordered_set<T, H, E> umap;
        public:
            typedef typename std::unordered_set<T, H, E>::iterator iterator;

            std::pair<iterator, bool> insert(T val) {
                std::lock_guard<std::mutex> lock(m);
                return umap.insert(std::move(val));
            }

            // Not concurrency safe!
            iterator find(T k) {
                return umap.find(k);
            }
            iterator end() {
                return umap.end();
            }
            std::size_t size() {
                return umap.size();
            }
            iterator begin() {
                return umap.begin();
            }
        };
    }
}