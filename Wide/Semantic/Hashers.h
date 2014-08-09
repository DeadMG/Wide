#pragma once

#include <vector>
#include <unordered_set>

namespace clang {
    class QualType;
}
namespace Wide {
    namespace Semantic {
        struct Type;
        struct BaseType;
        struct VectorTypeHasher {
            template<typename X, typename Y> std::size_t operator()(const std::vector<std::pair<X, Y>>& t) const {
                std::size_t hash = 0;
                for (auto ty : t)
                    hash += std::hash<X>()(ty.first) ^ std::hash<Y>()(ty.second);
                return hash;                
            }
            template<typename T> std::size_t operator()(const std::vector<T*>& t) const {
                std::size_t hash = 0;
                for (auto ty : t)
                    hash += std::hash<T*>()(ty);
                return hash;
            }
        };
        struct SetTypeHasher {
            template<typename T> std::size_t operator()(const std::unordered_set<T*>& t) const {
                std::size_t hash = 0;
                for (auto ty : t)
                    hash += std::hash<T*>()(ty);
                return hash;
            }
        };
        struct ClangTypeHasher {
            std::size_t operator()(clang::QualType t) const;
        };
        struct PairTypeHasher {
            std::size_t operator()(std::pair<Type*, Type*> types) const {
                return std::hash<Type*>()(types.first) ^ std::hash<Type*>()(types.second);
            }
        };
        struct PairTypeEquality {
            bool operator()(std::pair<Type*, Type*> lhs, std::pair<Type*, Type*> rhs) const {
                return (lhs.first == rhs.first && lhs.second == rhs.second) || (lhs.first == rhs.second && lhs.second == rhs.first);
            }
        };
    }
}