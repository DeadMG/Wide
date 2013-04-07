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
            clang::QualType GetClangType(ClangUtil::ClangTU& tu);

            Type* IsReference() {
                return Pointee;
            }
            bool IsReference(Type* t) {
                return Pointee == t;
            }
            Codegen::Expression* BuildBooleanConversion(Expression, Analyzer&);

            Expression BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a);            
            Expression BuildNEComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildValue(Expression ref, Analyzer& a);

        };
    }
}