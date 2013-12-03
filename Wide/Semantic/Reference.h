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

            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c) override;

            bool IsReference() override {
                return true;
            }
            bool IsReference(Type* to) override {
                return to == Pointee;
            }
            Type* GetContext(Analyzer& a) override {
                return Pointee->GetContext(a);
            }

            ConcreteExpression BuildRvalueConstruction(std::vector<ConcreteExpression> args, Context c) override;
            ConcreteExpression BuildLvalueConstruction(std::vector<ConcreteExpression> args, Context c) override;
            
            virtual Type* Decay() override {
                return Pointee;
            }

            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
        };
        class LvalueType : public Reference {
        public:
            LvalueType(Type* t) : Reference(t) {}
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) override;
            bool IsA(Type* other) override final;
        };
        class RvalueType : public Reference {
        public:
            RvalueType(Type* t) : Reference(t) {}
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) override;
            bool IsA(Type* other) override final;
        };
    }
}