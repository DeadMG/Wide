#pragma once

#include <Wide/Semantic/Type.h>
#include <unordered_map>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/AST/Expr.h>
#pragma warning(pop)

namespace clang {
    class LookupResult;
}
namespace Wide {
    namespace Semantic {       
        class ClangType : public Type {
            std::unordered_map<std::string, Type*> LookupResultCache;
            ConcreteExpression BuildOverloadSet(ConcreteExpression self, std::string name, clang::LookupResult& lr, Analyzer& a);
            ClangUtil::ClangTU* from;
            clang::QualType type;
        public:
            ClangType(ClangUtil::ClangTU* src, clang::QualType t);         
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;            
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) override;  

            Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression val, std::string name, Analyzer& a) override;
            Expression BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Analyzer& a) override;
            ConcreteExpression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType type, Analyzer& a) override;
            
            Wide::Codegen::Expression* BuildBooleanConversion(ConcreteExpression self, Analyzer& a) override;

            bool IsComplexType() override;
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a) override;
            ConversionRank RankConversionFrom(Type* from, Analyzer& a) override;
            ConcreteExpression BuildDereference(ConcreteExpression obj, Analyzer& a) override;
            ConcreteExpression BuildIncrement(ConcreteExpression obj, bool postfix, Analyzer& a) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
        };
    }
}