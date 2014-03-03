#include <Wide/Semantic/Type.h>
#include <unordered_map>

namespace Wide {
    namespace Semantic {
        class IntegralType : public PrimitiveType {
            unsigned bits;
            bool is_signed;

            void Extend(ConcreteExpression& lhs, ConcreteExpression& rhs, Analyzer& a);
        public:
            IntegralType(unsigned bit, bool sign)
                : bits(bit), is_signed(sign) {}
            
            clang::QualType GetClangType(ClangTU& TU, Analyzer& a) override final;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;


            OverloadSet* CreateADLOverloadSet(Lexer::TokenType name, Type* lhs, Type* rhs, Lexer::Access access, Analyzer& a) override final;
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            bool IsA(Type* self, Type* other, Analyzer& a, Lexer::Access access) override final;
            OverloadSet* CreateConstructorOverloadSet(Wide::Semantic::Analyzer&, Lexer::Access access) override final;
            OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Lexer::Access access, Analyzer& a) override final;
            std::string explain(Analyzer& a) override final;
        };
    }
}