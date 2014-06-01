#pragma once

#include <string>

namespace Wide {
    namespace AST {
        struct FunctionBase;
    }
    namespace Semantic {
        class Analyzer;
        class Module;
        std::string GetFunctionName(const Wide::AST::FunctionBase* func, Analyzer& a, std::string context, Wide::Semantic::Module* root);
    }
}