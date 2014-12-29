#include <tuple>
#include <Wide/Util/Indices.h>

namespace Wide {
    namespace Util {
        struct HashCombiner {
            HashCombiner() : value(0) {}
            HashCombiner(std::size_t hash) : value(hash) {}
            std::size_t value;
            HashCombiner operator()(std::size_t nexthash) const {
                return{ value ^ nexthash };
            }
        };
        struct HashPermuter {
            HashPermuter() : value(0) {}
            HashPermuter(std::size_t hash) : value(hash) {}
            std::size_t value;
            HashPermuter operator()(std::size_t nexthash) const {
                return{ (31 * value) + nexthash };
            }
        };
        template<typename Algorithm, typename Iterator> Algorithm hash_range(Algorithm A, Iterator begin, Iterator end) {
            if (begin == end) return A;
            A = A(std::hash<typename Iterator::value_type>()(*begin));
            ++begin;
            return hash(A, begin, end);
        }
        template<typename Algorithm, typename T, typename... Other> Algorithm hash(Algorithm A, T&& arg) {
            return A(std::hash<typename std::decay<T>::type>()(std::forward<T>(arg)));
        }
        template<typename Algorithm, typename T, typename... Other> Algorithm hash(Algorithm A, T&& arg, Other&&... other) {            
            return hash(A(std::hash<typename std::decay<T>::type>()(std::forward<T>(arg))), std::forward<Other>(other)...);
        }
        template<typename... T, unsigned... Is, typename Algorithm> Algorithm hash(Algorithm A, std::tuple<T...> args, indices<Is...>) {
            return hash(A, std::get<Is>(args)...);
        }
        template<typename... T, typename Algorithm> Algorithm hash(Algorithm A, std::tuple<T...> args) {
            return hash(A, std::move(args), indices_gen<sizeof...(T)>());
        }
    }
}