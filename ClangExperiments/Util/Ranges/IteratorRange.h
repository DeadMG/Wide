#pragma once

#include <iterator>
#include "Optional.h"

namespace Wide {
    namespace Range {
        namespace detail {
            template<typename Iterator> struct iterator_range {
                iterator_range(Iterator b, Iterator e)
                    : begin(std::move(b)), end(std::move(e)) {}
                Iterator begin, end;
                Util::optional<typename std::iterator_traits<Iterator>::value_type> operator()() {
                    if (begin == end)
                        return Util::none;
                    return *begin++;
                }
            };
        }
        template<typename Iterator> detail::iterator_range<Iterator> IteratorRange(Iterator begin, Iterator end) {
            return detail::iterator_range<Iterator>(begin, end);
        }
        template<typename Container> auto IteratorRange(Container&& obj) -> detail::iterator_range<decltype(std::begin(obj))> {
            return detail::iterator_range<decltype(std::begin(obj))>(std::begin(obj), std::end(obj));
        }
    }
}