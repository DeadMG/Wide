#pragma once

#include "PrimitiveType.h"

namespace Wide {
    namespace Semantic {
        struct NullType : public PrimitiveType {    
            NullType() {}
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
                if (args.size() == 0)
                    return mem;
                if (args.size() == 1 && args[0].t->Decay() == this)
                    return mem;
                throw std::runtime_error("Attempted to construct a null with something that was not a null, or more than one argument.");
            }
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            std::size_t size(Analyzer& a);
            std::size_t alignment(Analyzer& a);
        };
    }
}