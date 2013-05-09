#include "PrimitiveType.h"

namespace Wide {
    namespace Semantic {
        class IntegralType : public PrimitiveType {
            unsigned bits;
        public:
            IntegralType(unsigned bit)
                : bits(bit) {}
            
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            
            Expression BuildRightShift(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a);
        };
    }
}