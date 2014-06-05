#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class Reference : public Type {
            Type* Pointee;
        public:
            Reference(Type* p, Analyzer& a)
                : Pointee(p), Type(a) {}

            llvm::Type* GetLLVMType(llvm::Module* module) override final;
            
            bool IsReference() override final {
                return true;
            }
            bool IsReference(Type* to) override final {
                return to == Pointee;
            }
            Type* GetContext() override final {
                return Pointee->GetContext();
            }
                        
            virtual Type* Decay() override final {
                return Pointee;
            }

            std::size_t size() override;
            std::size_t alignment() override;
            virtual bool IsA(Type* self, Type* other, Lexer::Access access) override = 0;
            bool IsCopyConstructible(Lexer::Access access) override final { return true; }
            bool IsMoveConstructible(Lexer::Access access) override final { return true; }
            bool IsCopyAssignable(Lexer::Access access) override final { return false; }
            bool IsMoveAssignable(Lexer::Access access) override final { return false; }
        };
        class LvalueType : public Reference {
            std::unique_ptr<OverloadResolvable> DerivedConstructor;
            std::unique_ptr<OverloadResolvable> CopyMoveConstructor;
        public:
            LvalueType(Type* t, Analyzer& a) : Reference(t, a) {}
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& tu) override final;
            bool IsA(Type* self, Type* other, Lexer::Access access) override final;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override final;
            std::string explain() override final;
        };
        class RvalueType : public Reference {
            std::unique_ptr<OverloadResolvable> RvalueConvertible;
            std::unique_ptr<OverloadResolvable> CopyMoveConstructor;
        public:
            RvalueType(Type* t, Analyzer& a) : Reference(t, a) {}
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& tu) override final;
            bool IsA(Type* self, Type* other, Lexer::Access access) override final;
            OverloadSet* CreateConstructorOverloadSet(Lexer::Access access) override final;
            std::string explain() override final;
        };
    }
}