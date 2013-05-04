#pragma once

#include "Type.h"

namespace Wide {
    namespace Semantic {                
        class LvalueType : public Type {
            Type* Pointee;
        public:
            LvalueType(Type* p)
                : Pointee(p) {}
            
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);        
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a);        
            
            Expression BuildRightShift(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildCall(Expression e, std::vector<Expression> args, Analyzer& a) {
                return Pointee->BuildCall(e, std::move(args), a);
            }        
            Expression AccessMember(Expression val, std::string name, Analyzer& a) {
                return Pointee->AccessMember(val, std::move(name), a);
            }            
            Expression BuildAssignment(Expression lhs, Expression rhs, Analyzer& analyzer);        
            Expression BuildValue(Expression lhs, Analyzer& analyzer);     
            Type* IsReference() {
                return Pointee;
            }
            bool IsReference(Type* t) {
                return Pointee == t;
            }
            Codegen::Expression* BuildBooleanConversion(Expression, Analyzer&);
            Expression BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildNEComparison(Expression lhs, Expression rhs, Analyzer& a);
            
            Expression BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildLTEComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildGTComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildGTEComparison(Expression lhs, Expression rhs, Analyzer& a);
            ConversionRank RankConversionFrom(Type* from, Analyzer& a) {
                assert(false && "Internal Compiler Error: All T& conversions should be dealt with by Analyzer.");
                // Just to shut up the compiler
                return ConversionRank::None;
            }
        };
    }
}