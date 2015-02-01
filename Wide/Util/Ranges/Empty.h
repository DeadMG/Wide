#pragma once

#include <Wide/Util/Ranges/Optional.h>

namespace Wide {
    namespace Range {
        struct EmptyRange {
            Util::none_t operator()() { return Util::none; }
        };
        inline EmptyRange Empty() {
            return EmptyRange();
        }
    }
}