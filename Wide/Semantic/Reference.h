#pragma once

#include <Wide/Semantic/Type.h>

namespace Wide {
    namespace Semantic {
        class Reference : public Type {
            Type* Pointee;
        public:
            Reference(Type* p)
                : Pointee(p) {}

            std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a);
            clang::QualType GetClangType(ClangUtil::ClangTU& tu, Analyzer& a);

            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a);
            Expression BuildRightShift(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildCall(Expression e, std::vector<Expression> args, Analyzer& a) {
                return Pointee->BuildCall(e, std::move(args), a);
            }
            Expression AccessMember(Expression val, std::string name, Analyzer& a) {
                return Pointee->AccessMember(val, std::move(name), a);
            }
            Expression BuildAssignment(Expression lhs, Expression rhs, Analyzer& analyzer);
            Expression BuildValue(Expression lhs, Analyzer& analyzer);
            Type* IsReference() {
                return Pointee;
            }
            bool IsReference(Type* t) {
                return Pointee == t;
            }
            Codegen::Expression* BuildBooleanConversion(Expression, Analyzer&);
            AST::DeclContext* GetDeclContext() { return Pointee->GetDeclContext(); }
            Expression BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildNEComparison(Expression lhs, Expression rhs, Analyzer& a);

            Expression BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildLTEComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildGTComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildGTEComparison(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildDereference(Expression obj, Analyzer& a);
            Expression PointerAccessMember(Expression obj, std::string name, Analyzer& a);
            Expression BuildRvalueConstruction(std::vector<Expression> args, Analyzer& a);
            Expression BuildLvalueConstruction(std::vector<Expression> args, Analyzer& a);

            ConversionRank RankConversionFrom(Type* from, Analyzer& a) {
                assert(false && "Internal Compiler Error: All T& conversions should be dealt with by Analyzer.");
                // Just to shut up the compiler
                return ConversionRank::None;
            }
            std::size_t size(Analyzer& a);
            std::size_t alignment(Analyzer& a);
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