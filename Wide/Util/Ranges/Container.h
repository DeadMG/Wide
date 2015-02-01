#pragma once

#include <Wide/Util/Ranges/Optional.h>

namespace Wide {
    namespace Range {
        template<typename Cont> struct ContRange {
            Cont c;
            typename Cont::iterator begin;
            typename Cont::iterator end;
            ContRange(Cont con)
                : c(std::move(con)), begin(c.begin()), end(c.end()) {}
            ContRange(const ContRange& other)
                : c(other.begin, other.end), begin(c.begin()), end(c.end()) {}
            Util::optional<typename Cont::value_type> operator()() {
                if (begin == end)
                    return Util::none;
                auto val = *begin;
                ++begin;
                return val;
            }
        };
        template<typename Cont> ContRange<Cont> Container(Cont c) {
            return ContRange<Cont>(std::move(c));
        }
    }
}