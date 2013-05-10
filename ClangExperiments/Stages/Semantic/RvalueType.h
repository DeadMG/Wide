#include "Type.h"

namespace Wide {
    namespace Semantic {
        class RvalueType : public Type {
            Type* Pointee;
        public:
            RvalueType(Type* t)
                : Pointee(t) {}

            Type* GetPointee() {
                return Pointee;
            }
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
            Expression BuildAssignment(Expression lhs, Expression rhs, Analyzer& analyzer) {
                return Pointee->BuildAssignment(lhs, rhs, analyzer);
            }
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
            Expression BuildDereference(Expression obj, Analyzer& a) {
                return Pointee->BuildDereference(obj, a);
            }
            ConversionRank RankConversionFrom(Type* from, Analyzer& a) {
                // This will only ever get called when a.RankConversion(from, to) is called where to is T&&
                // and from is U. This is the same as the rank of U to T, so just return that.
                return Pointee->RankConversionFrom(from, a);
            }
        };
    }
}