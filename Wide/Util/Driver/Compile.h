#pragma once
#include <initializer_list>
#include <string>
#include <vector>
#include <functional>

namespace Wide {
    namespace Options {
        struct Clang;
    }
    namespace Semantic {
        class Analyzer;
    }
    namespace Codegen {
        class Generator;
    }
    namespace AST {
        struct Module;
    }
    namespace Driver {
        void Compile(const Wide::Options::Clang& copts, std::function<void(Semantic::Analyzer&, const AST::Module*)>, Codegen::Generator& gen, std::initializer_list<std::string> files);
        void Compile(const Wide::Options::Clang& copts, std::function<void(Semantic::Analyzer&, const AST::Module*)>, Codegen::Generator& gen, const std::vector<std::string>& files);
    }
}