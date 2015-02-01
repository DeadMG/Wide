#pragma once

#include <Wide/Util/Ranges/Optional.h>

namespace Wide {
    namespace Range {
        template<typename R, typename F> struct map {
            F f;
            R r;
        public:
            map(F f, R r)
                : f(std::move(f)), r(std::move(r)) {}
            auto operator()() -> Util::optional<typename std::decay<decltype(f(*r()))>::type> {
                auto val = r();
                if (!val) return Util::none;
                return f(*std::move(val));
            }
        };
        template<typename F> struct map_pipe {
            map_pipe(F f)
                : f(std::move(f)) {}
            F f;
            template<typename R> map<R, F> operator()(R r) {
                return map<R, F>(std::move(f), std::move(r));
            }
        };
        template<typename F> map_pipe<F> Map(F f) {
            return map_pipe<F>(f);
        }
    }
}