#pragma once

#include <utility>

namespace Wide {
    namespace Range {
        template<typename F> struct copy_pipe {
            copy_pipe(F f) 
                : f(std::move(f)) {}
            F f;
            template<typename R> void operator()(R r) {
                while (auto val = r())
                    f(*val);
            }
        };
        template<typename F> copy_pipe<F> Copy(F f) {
            return copy_pipe<F>(std::move(f));
        }
    }
}