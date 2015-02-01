#pragma once

#include <array>
#include <Wide/Util/Ranges/Optional.h>
#include <Wide/Util/Ranges/Container.h>

namespace Wide {
    namespace Range {
        template<typename... Args> auto Elements(Args&&... args) -> ContRange<std::vector<typename std::common_type<typename std::decay<Args>::type...>::type>> {
            typedef std::vector<typename std::common_type<typename std::decay<Args>::type...>::type> RetArray;
            RetArray arr = { { std::forward<Args>(args)... } };
            return ContRange<RetArray>(arr);
        }
    }
}