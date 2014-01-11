#include <Wide/Semantic/Type.h>
#include <unordered_map>

namespace Wide {
    namespace Semantic {
        class IntegralType : public Type {
            std::unordered_map<Lexer::TokenType, OverloadSet*> callables;
            unsigned bits;
            bool is_signed;

            void Extend(ConcreteExpression& lhs, ConcreteExpression& rhs, Analyzer& a);
        public:
            IntegralType(unsigned bit, bool sign)
                : bits(bit), is_signed(sign) {}
            
            clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a) override final;
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override final;
            
            OverloadSet* PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Context c) override final;
            ConcreteExpression BuildIncrement(ConcreteExpression obj, bool postfix, Context c) override final;
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c) override final;
            std::size_t size(Analyzer& a) override final;
            std::size_t alignment(Analyzer& a) override final;
            bool IsA(Type* other, Analyzer& a) override final;
        };
    }
}