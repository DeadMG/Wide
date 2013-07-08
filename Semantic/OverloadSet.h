#pragma once

#include <Semantic/PrimitiveType.h>
#include <memory>
#include <unordered_map>

#ifndef _MSC_VER
#pragma warning(push, 0)
#include <clang/AST/Type.h>
#pragma warning(pop)
#endif

namespace Wide {
    namespace AST {
        struct FunctionOverloadSet;
    }
    namespace Semantic {
        class Function;
        class UserDefinedType;
        class OverloadSet : public PrimitiveType {
            AST::FunctionOverloadSet* overset;
            std::unordered_map<ClangUtil::ClangTU*, clang::QualType> clangtypes;
            Type* nonstatic;
        public:
            OverloadSet(AST::FunctionOverloadSet* s, Analyzer& a, Type* nonstatic = nullptr);
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);
            Expression BuildCall(Expression, std::vector<Expression> args, Analyzer& a);
            Expression BuildValueConstruction(std::vector<Expression> args, Analyzer& a);
            ConversionRank ResolveOverloadRank(std::vector<Type*> types, Analyzer& a);
            std::size_t size(Analyzer& a);
            std::size_t alignment(Analyzer& a);

            using Type::BuildValueConstruction;
        };
    }
}