#pragma once

#include <Wide/Util/Ranges/Concat.h>
#include <Wide/Util/Ranges/Pipe.h>
#include <Wide/Util/Ranges/Map.h>
#include <Wide/Util/Ranges/IteratorRange.h>
#include <Wide/Util/Ranges/Copy.h>
#include <Wide/Util/Ranges/Container.h>
#include <Wide/Util/Ranges/ElementRange.h>
#include <Wide/Util/Ranges/Empty.h>
#include <functional>

namespace Wide {
    namespace Range {
        template<typename T> struct Erased {
            template<typename F> Erased(F f)
                : func(std::move(f)) {}
            Erased(std::function<Wide::Util::optional<T>()> f)
                : func(std::move(f)) {}
            std::function<Wide::Util::optional<T>()> func;

            Wide::Util::optional<T> operator()() {
                return func();
            }
        };
    }
}