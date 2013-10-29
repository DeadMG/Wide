#include <Wide/Semantic/Type.h>
#include <unordered_map>

namespace Wide {
    namespace Semantic {
        class IntegralType : public Type {
            std::unordered_map<Lexer::TokenType, OverloadSet*> callables;
            unsigned bits;
            bool is_signed;

            void Extend(Expression& lhs, Expression& rhs, Analyzer& a);
        public:
            IntegralType(unsigned bit, bool sign)
                : bits(bit), is_signed(sign) {}
            
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;
            
            OverloadSet* AccessMember(ConcreteExpression expr, Lexer::TokenType name, Analyzer& a, Lexer::Range where) override;
            ConcreteExpression BuildIncrement(ConcreteExpression obj, bool postfix, Analyzer& a, Lexer::Range where) override;
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Analyzer& a, Lexer::Range where) override;
            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
        };
    }
}