#pragma once

#include <string>

namespace Wide {
    namespace Parse {
        struct FunctionBase;
    }
    namespace Semantic {
        class Analyzer;
        class Module;
        std::string GetFunctionName(const Wide::Parse::FunctionBase* func, Analyzer& a, std::string context, Wide::Semantic::Module* root);
    }
}