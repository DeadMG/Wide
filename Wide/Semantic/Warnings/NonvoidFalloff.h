#pragma once

#include <string>
#include <vector>
#include <utility>

namespace Wide {
    namespace AST {
        struct FunctionBase;
    }
    namespace Lexer {
        struct Range;
    }
    namespace Semantic {
        class Analyzer;
        std::vector<std::tuple<Lexer::Range, std::string>> GetNonvoidFalloffFunctions(Analyzer& a);
    }
}
