#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class Reference : public Type {
            Type* Pointee;
        public:
            Reference(Type* p)
                : Pointee(p) {}
            
            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) override;
            
            bool IsReference() override {
                return true;
            }
            bool IsReference(Type* to) override {
                return to == Pointee;
            }
            Type* GetContext(Analyzer& a) override {
                return Pointee->GetContext(a);
            }
                        
            virtual Type* Decay() override {
                return Pointee;
            }

            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
            bool IsA(Type* other, Analyzer& a) override = 0;
            bool IsCopyConstructible(Analyzer& a) { return true; }
            bool IsMoveConstructible(Analyzer& a) { return true; }
            bool IsCopyAssignable(Analyzer& a) { return false; }
            bool IsMoveAssignable(Analyzer& a) { return false; }
        };
        class LvalueType : public Reference {
        public:
            LvalueType(Type* t) : Reference(t) {}
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) override;
            bool IsA(Type* other, Analyzer& a) override final;
            OverloadSet* CreateConstructorOverloadSet(Analyzer& a) override final;
        };
        class RvalueType : public Reference {
        public:
            RvalueType(Type* t) : Reference(t) {}
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) override;
            bool IsA(Type* other, Analyzer& a) override final;
            OverloadSet* CreateConstructorOverloadSet(Analyzer& a) override final;
            ConcreteExpression BuildValueConstruction(std::vector<ConcreteExpression> args, Context c) override final;
        };
    }
}