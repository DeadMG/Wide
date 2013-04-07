#pragma once

#include <memory>

namespace Wide {
    namespace Util {
        // When variadics not fucked
        /*
        template<typename T, typename... Args> std::unique_ptr<T> make_unique(Args&&... args) {
            return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
        }*/
        template<typename T> std::unique_ptr<T> make_unique() {
            return std::unique_ptr<T>(new T());
        }
        template<typename T, typename T1> std::unique_ptr<T> make_unique(T1&& arg1) {
            return std::unique_ptr<T>(new T(std::forward<T1>(arg1)));
        }
        template<typename T, typename T1, typename T2> std::unique_ptr<T> make_unique(T1&& arg1, T2&& arg2) {
            return std::unique_ptr<T>(new T(std::forward<T1>(arg1), std::forward<T2>(arg2)));
        }
        template<typename T, typename T1, typename T2, typename T3> std::unique_ptr<T> make_unique(T1&& arg1, T2&& arg2, T3&& arg3) {
            return std::unique_ptr<T>(new T(std::forward<T1>(arg1), std::forward<T2>(arg2), std::forward<T3>(arg3)));
        }
        template<typename T, typename T1, typename T2, typename T3, typename T4> std::unique_ptr<T> make_unique(T1&& arg1, T2&& arg2, T3&& arg3, T4&& arg4) {
            return std::unique_ptr<T>(new T(std::forward<T1>(arg1), std::forward<T2>(arg2), std::forward<T3>(arg3), std::forward<T4>(arg4)));
        }
        template<typename T, typename T1, typename T2, typename T3, typename T4, typename T5> std::unique_ptr<T> make_unique(T1&& arg1, T2&& arg2, T3&& arg3, T4&& arg4, T5&& arg5) {
            return std::unique_ptr<T>(new T(std::forward<T1>(arg1), std::forward<T2>(arg2), std::forward<T3>(arg3), std::forward<T4>(arg4), std::forward<T5>(arg5)));
        }
        template<typename T, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6> std::unique_ptr<T> make_unique(T1&& arg1, T2&& arg2, T3&& arg3, T4&& arg4, T5&& arg5, T6&& arg6) {
            return std::unique_ptr<T>(new T(std::forward<T1>(arg1), std::forward<T2>(arg2), std::forward<T3>(arg3), std::forward<T4>(arg4), std::forward<T5>(arg5), std::forward<T6>(arg6)));
        }
    }
}