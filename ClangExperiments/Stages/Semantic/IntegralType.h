#include "PrimitiveType.h"

namespace Wide {
    namespace Semantic {
        class IntegralType : public PrimitiveType {
            unsigned bits;
            bool is_signed;

            void Extend(Expression& lhs, Expression& rhs, Analyzer& a);
        public:
            IntegralType(unsigned bit, bool sign)
                : bits(bit), is_signed(sign) {}
            
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            
            Expression BuildRightShift(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildMultiply(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildPlus(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildIncrement(Expression obj, bool postfix, Analyzer& a);
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a);
            std::size_t size(Analyzer& a);
            std::size_t alignment(Analyzer& a);
        };
    }
}