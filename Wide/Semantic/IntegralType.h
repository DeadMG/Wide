#include <Wide/Semantic/Type.h>
#include <unordered_map>

namespace Wide {
    namespace Semantic {
        class IntegralType : public PrimitiveType {
            unsigned bits;
            bool is_signed;

            std::unique_ptr<OverloadResolvable> ConvertingConstructor;
            std::unique_ptr<OverloadResolvable> RightShiftAssign;
            std::unique_ptr<OverloadResolvable> RightShift;
            std::unique_ptr<OverloadResolvable> LeftShiftAssign;
            std::unique_ptr<OverloadResolvable> LeftShift;
            std::unique_ptr<OverloadResolvable> MulAssign;
            std::unique_ptr<OverloadResolvable> Mul;
            std::unique_ptr<OverloadResolvable> PlusAssign;
            std::unique_ptr<OverloadResolvable> Plus;
            std::unique_ptr<OverloadResolvable> OrAssign;
            std::unique_ptr<OverloadResolvable> Or;
            std::unique_ptr<OverloadResolvable> AndAssign;
            std::unique_ptr<OverloadResolvable> And;
            std::unique_ptr<OverloadResolvable> XorAssign;
            std::unique_ptr<OverloadResolvable> Xor;
            std::unique_ptr<OverloadResolvable> MinusAssign;
            std::unique_ptr<OverloadResolvable> Minus;
            std::unique_ptr<OverloadResolvable> ModAssign;
            std::unique_ptr<OverloadResolvable> Mod;
            std::unique_ptr<OverloadResolvable> DivAssign;
            std::unique_ptr<OverloadResolvable> Div;
            std::unique_ptr<OverloadResolvable> LT;
            std::unique_ptr<OverloadResolvable> EQ;
            std::unique_ptr<OverloadResolvable> Increment;
        public:
            IntegralType(unsigned bit, bool sign, Analyzer& a);
            
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& TU) override final;
            llvm::Type* GetLLVMType(llvm::Module* module) override final;

            OverloadSet* CreateADLOverloadSet(Lexer::TokenType name, Type* lhs, Type* rhs, Lexer::Access access) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            bool IsA(Type* self, Type* other, Lexer::Access access) override final;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override final;
            OverloadSet* CreateOperatorOverloadSet(Type* self, Lexer::TokenType what, Lexer::Access access) override final;
            std::string explain() override final;
        };
    }
}