#pragma once

#include <string>
#include <vector>

namespace Wide {
    namespace Options {
        struct Clang;
    }
    namespace Driver {
        std::vector<std::string> GetGCCIncludePaths(std::string);
    }
}