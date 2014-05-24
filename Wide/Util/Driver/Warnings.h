#pragma once

#include <unordered_set>
#include <string>

namespace Wide {
    namespace Semantic {
        class Analyzer;
    }
    namespace Driver {
        void PrintUnusedFunctions(const std::unordered_set<std::string>& files, Semantic::Analyzer& a);
    }
}