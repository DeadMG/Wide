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
        std::vector<std::tuple<const AST::FunctionBase*, Lexer::Range, std::string>> GetUnusedFunctions(Analyzer& a);
    }
}
