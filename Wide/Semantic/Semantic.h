#pragma once

namespace Wide {
    namespace Options {
        struct Clang;
    }
    namespace AST {
        struct Module;
    }
    namespace Codegen {
        class Generator;
    }
    namespace Semantic {
        void Analyze(const AST::Module* root, const Options::Clang&, Codegen::Generator& g);
    }
}