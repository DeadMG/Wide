#pragma once

#include <vector>
#include <unordered_set>

namespace clang {
    class QualType;
}
namespace Wide {
    namespace Semantic {
        struct Type;
        struct VectorTypeHasher {
            std::size_t operator()(const std::vector<Type*>& t) const;
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
    }
}