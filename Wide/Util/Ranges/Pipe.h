#pragma once

#include <utility>

namespace Wide {
    namespace Range {
        template<typename X, typename Y> auto operator|(X x, Y y) -> decltype(y(std::move(x))) {
            return y(std::move(x));
        }
    }
}