#pragma once

#include <Wide/Util/Concurrency/ConcurrentDetail/stdqueue.h>
#include <Wide/Util/DebugUtilities.h>
#include <Wide/Util/Concurrency/ConcurrentVector.h>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <exception>

namespace Wide {
    namespace Concurrency {
        template<typename Iterator, typename Func> std::vector<std::exception_ptr> ParallelForEach(Iterator begin, Iterator end, Func f) {
            std::vector<Iterator> its;
            while(begin != end)
                its.push_back(begin++);
            Queue<Iterator> its_queue(its.begin(), its.end());
            Vector<std::exception_ptr> errors;
            auto threadnum = std::thread::hardware_concurrency() + 1;
            std::vector<std::thread> threads;
            for(std::size_t i = 0; i < threadnum; ++i) {
                threads.emplace_back(std::thread([&] {
                    while(true) {
                        Iterator it;
                        if (!its_queue.try_pop(it))
                            break;
                        try {
                            f(*it);
                        } catch (...) {
                            errors.push_back(std::current_exception());
                        }
                    }
                }));
            }
            for(auto&& thr : threads)
                thr.join();
            return std::vector<std::exception_ptr>(errors.begin(), errors.end());
        }
    }
}
