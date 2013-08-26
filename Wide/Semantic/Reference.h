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
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a) override;

            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) override;

            bool IsReference() override {
                return true;
            }
            bool IsReference(Type* to) override {
                return to == Pointee;
            }
            Expression BuildRvalueConstruction(std::vector<Expression> args, Analyzer& a) override;
            Expression BuildLvalueConstruction(std::vector<Expression> args, Analyzer& a) override;

            ConversionRank RankConversionFrom(Type* from, Analyzer& a) override {
                assert(false && "Internal Compiler Error: All T& conversions should be dealt with by Analyzer.");
                // Just to shut up the compiler
                return ConversionRank::None;
            }

            virtual Type* Decay() override {
                return Pointee;
            }

            std::size_t size(Analyzer& a) override;
            std::size_t alignment(Analyzer& a) override;
        };
        class LvalueType : public Reference {
        public:
            LvalueType(Type* t) : Reference(t) {}
        };
        class RvalueType : public Reference {
        public:
            RvalueType(Type* t) : Reference(t) {}
        };
    }
}