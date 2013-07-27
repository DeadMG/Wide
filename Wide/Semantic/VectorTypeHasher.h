#pragma once

#include <vector>

namespace Wide {
    namespace Semantic {
        struct Type;
        struct VectorTypeHasher {
            std::size_t operator()(const std::vector<Type*>& t) const;
        };
    }
}