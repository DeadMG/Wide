#pragma once

#include "Optional.h"
#include <istream>

namespace Wide {
    namespace Range {
        struct IStreamRangeReturn {
            std::istream* stream;
            Wide::Util::optional<char> operator()() {
                char c;
                if ((*stream) >> c)
                    return c;
                return Util::none;
            }
        };
        IStreamRangeReturn IStreamRange(std::istream& stream) {
            IStreamRangeReturn r;
            r.stream = &stream;
            return r;
        }
    }
}