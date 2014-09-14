#pragma once
#include <string>

namespace Wide {
    namespace Paths {
        std::string Append(std::string one, std::string two);
        std::string Append(std::string a, std::initializer_list<std::string> others);
    }
}