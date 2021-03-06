#pragma once

#include <ppl.h>
#include <vector>
#include <exception>
#include <Wide/Util/Concurrency/ConcurrentVector.h>

namespace Wide {
    namespace Concurrency {
        template<typename Iterator, typename F> std::vector<std::exception_ptr> ParallelForEach(Iterator begin, Iterator end, F f) {
            Vector<std::exception_ptr> errors;
            ::Concurrency::parallel_for_each(begin, end, [&](decltype(*begin) arg) {
                try {
                    f(arg);
                } catch (...) {
                    errors.push_back(std::current_exception());
                }
            });
            return std::vector<std::exception_ptr>(errors.begin(), errors.end());
        }
    }
}