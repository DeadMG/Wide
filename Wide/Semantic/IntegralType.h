#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class IntegralType : public Type {
            unsigned bits;
            bool is_signed;

            void Extend(Expression& lhs, Expression& rhs, Analyzer& a);
        public:
            IntegralType(unsigned bit, bool sign)
                : bits(bit), is_signed(sign) {}
            
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;
            
            ConcreteExpression BuildIncrement(ConcreteExpression obj, bool postfix, Analyzer& a) override;
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
            ConcreteExpression BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType type, Analyzer& a) override;
        };
    }
}