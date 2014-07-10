#pragma once

#include <unordered_set>
#include <string>

namespace Wide {
    namespace Driver {
        std::unordered_set<std::string> SearchStdlibDirectory(std::string path, std::string triple);
    }
}