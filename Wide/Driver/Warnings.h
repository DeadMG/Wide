#pragma once

#include <unordered_set>
#include <string>

namespace Wide {
    namespace Semantic {
        class Analyzer;
    }
    namespace Driver {
        void PrintUnusedFunctionsWarning(const std::unordered_set<std::string>& files, Semantic::Analyzer& a);
        void PrintNonvoidFalloffWarning(const std::unordered_set<std::string>& files, Semantic::Analyzer& a);
    }
}