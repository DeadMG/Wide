#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class Reference : public Type {
            Type* Pointee;
        public:
            Reference(Type* p, Analyzer& a)
                : Pointee(p), Type(a) {}

            std::function<llvm::Constant*(llvm::Module*)> GetRTTI() override final;
            llvm::Type* GetLLVMType(llvm::Module* module) override final;
            
            bool IsReference() override final {
                return true;
            }
            bool IsReference(Type* to) override final {
                return to == Pointee;
            }                        
            virtual Type* Decay() override final {
                return Pointee;
            }

            std::size_t size() override;
            std::size_t alignment() override;
            virtual bool IsSourceATarget(Type* first, Type* second, Location context) override = 0;
            bool IsCopyConstructible(Location) override final { return true; }
            bool IsMoveConstructible(Location) override final { return true; }
            bool IsCopyAssignable(Location) override final { return false; }
            bool IsMoveAssignable(Location) override final { return false; }
        };
        class LvalueType : public Reference {
            std::unique_ptr<OverloadResolvable> DerivedConstructor;
            std::unique_ptr<OverloadResolvable> CopyMoveConstructor;
        public:
            LvalueType(Type* t, Analyzer& a) : Reference(t, a) {}
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& tu) override final;
            bool IsSourceATarget(Type* first, Type* second, Location context) override final;
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override final;
            std::string explain() override final;
        };
        class RvalueType : public Reference {
            std::unique_ptr<OverloadResolvable> RvalueConvertible;
            std::unique_ptr<OverloadResolvable> CopyMoveConstructor;
        public:
            RvalueType(Type* t, Analyzer& a) : Reference(t, a) {}
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& tu) override final;
            bool IsSourceATarget(Type* first, Type* second, Location context) override final;
            OverloadSet* CreateConstructorOverloadSet(Parse::Access access) override final;
            std::string explain() override final;
        };
    }
}