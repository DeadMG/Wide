#pragma once

#include <Semantic/Type.h>
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
            Expression BuildOverloadSet(Expression self, std::string name, clang::LookupResult& lr, Analyzer& a);
            Expression BuildOverloadedOperator(Expression lhs, Expression rhs, Analyzer& a, clang::OverloadedOperatorKind, clang::BinaryOperatorKind);
            ClangUtil::ClangTU* from;
            clang::QualType type;
        public:
            ClangType(ClangUtil::ClangTU* src, clang::QualType t);         
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);            
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a);  

            Expression AccessMember(Expression val, std::string name, Analyzer& a);        
            Expression BuildCall(Expression val, std::vector<Expression> args, Analyzer& a);        
            Expression BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a);    
            Expression BuildRightShift(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildAssignment(Expression, Expression, Analyzer&);
            Expression BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a);            
            Expression BuildNEComparison(Expression lhs, Expression rhs, Analyzer& a);             
            Expression BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildLTEComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildGTComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildGTEComparison(Expression lhs, Expression rhs, Analyzer& a);
            Wide::Codegen::Expression* BuildBooleanConversion(Expression self, Analyzer& a);

            bool IsComplexType();
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a);
            ConversionRank RankConversionFrom(Type* from, Analyzer& a);
            Expression BuildDereference(Expression obj, Analyzer& a);
            Codegen::Expression* BuildDestructor(Expression obj, Analyzer& a);
            Expression BuildIncrement(Expression obj, bool postfix, Analyzer& a);
            std::size_t size(Analyzer& a);
            std::size_t alignment(Analyzer& a);
        };
    }
}