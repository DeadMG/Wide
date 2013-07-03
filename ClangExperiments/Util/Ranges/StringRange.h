#pragma once

#include <string>
#include "IteratorRange.h"

namespace Wide {
    namespace Range {
        struct stringrange : public  {
            std::string value;
            detail::IteratorRange<std::string::iterator> range;
            stringrange(std::string val)
                : value(std::move(val))
                , range(value.begin(), value.end())
            {}
            Util::optional<char> operator()() {
                return range();
            }
        };
        stringrange StringRange(std::string val) {
            return stringrange(std::move(val));
        }
    }
}