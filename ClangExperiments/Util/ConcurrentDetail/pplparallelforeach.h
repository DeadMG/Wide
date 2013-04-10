#pragma once

#include <ppl.h>

namespace Wide {
    namespace Concurrency {
        template<typename Iterator, typename F> void ParallelForEach(Iterator begin, Iterator end, F f) {
            return ::Concurrency::parallel_for_each(begin, end, f);
        }
    }
}