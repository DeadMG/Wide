#pragma once

#include <Wide/Util/Ranges/Optional.h>

namespace Wide {
    namespace Range {
        template<typename X, typename Y> struct concat {
            bool done;
            X x;
            Y y;
        public:
            concat(X x, Y y)
                : x(std::move(x)), y(std::move(y)) 
            {
                done = false;
            }
            auto operator()() -> Util::optional<typename std::decay<decltype(*x())>::type>  {
                if (!done) {
                    if (auto var = x())
                        return var;
                    done = true;
                }
                return y();
            }
        };
        template<typename Y> struct concat_pipe {
            Y y;
        public:
            concat_pipe(Y y)
                : y(std::move(y)) {}
            template<typename X> concat<X, Y> operator()(X x) {
                return concat<X, Y>(std::move(x), std::move(y));
            }
        };
        template<typename T> concat_pipe<T> Concat(T t) {
            return concat_pipe<T>(std::move(t));
        }
    }
}