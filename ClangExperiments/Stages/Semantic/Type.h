#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include <vector>
#include <stdexcept>
#include <functional>
#include <string>
#include <cassert>

namespace llvm {
    class Type;
    class LLVMContext;
    class Module;
}
namespace clang {
    class QualType;
}
namespace Wide {
    namespace ClangUtil {
        class ClangTU;
    }
    namespace Codegen {
        struct Expression;
        class Generator;
    }
    namespace AST {
        struct DeclContext;
    }
    namespace Semantic {
        class Analyzer;
        struct Type;
        struct Expression {
            Expression()
                : t(nullptr)
                , Expr(nullptr)
                , steal(false) {}
            Expression(Type* ty, Codegen::Expression* ex)
                : t(ty), Expr(ex), steal(false) {}
            Type* t;
            Codegen::Expression* Expr;
            bool steal;
        };

        enum ConversionRank {
            // No-cost conversion like reference binding or exact match
            Zero,

            // Derived-to-base conversion and such
            One,

            // User-defined implicit conversion
            Two,

            // No conversion possible
            None,
        };
        struct Type  {
        public:
            virtual bool IsReference(Type*) {
                return false;
            }
            virtual Type* IsReference() {
                return nullptr;
            }
            virtual AST::DeclContext* GetDeclContext() {
                return nullptr;
            }
            virtual bool IsComplexType() { return false; }
            virtual clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);
            virtual std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) {
                throw std::runtime_error("This type has no LLVM counterpart.");
            }
            Type* Decay() {
                if (IsReference())
                    return IsReference();
                return this;
            }
            virtual Expression BuildValueConstruction(std::vector<Expression> args, Analyzer& a);
            virtual Expression BuildRvalueConstruction(std::vector<Expression> args, Analyzer& a);
            virtual Expression BuildLvalueConstruction(std::vector<Expression> args, Analyzer& a);
            virtual Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a) {
                throw std::runtime_error("Could not inplace construct this type.");
            }
            virtual Expression BuildValue(Expression lhs, Analyzer& a) {
                if (IsComplexType())
                    throw std::runtime_error("Internal Compiler Error: Attempted to build a complex type into a register.");
                return lhs;
            }            

            virtual Expression BuildRightShift(Expression lhs, Expression rhs, Analyzer& a) {
                throw std::runtime_error("Attempted to right shift with a type that did not support it.");
            }
            virtual Expression BuildLeftShift(Expression lhs, Expression rhs, Analyzer& a) {
                throw std::runtime_error("Attempted to left shift with a type that did not support it.");
            }
            virtual Expression BuildAssignment(Expression lhs, Expression rhs, Analyzer& a) {
                throw std::runtime_error("Attempted to assign to a type that did not support it.");
            }            
            virtual Expression AccessMember(Expression, std::string name, Analyzer& a) {
                throw std::runtime_error("Attempted to access the member of a type that did not support it.");
            }
            virtual Expression BuildCall(Expression val, std::vector<Expression> args, Analyzer& a) {
                throw std::runtime_error("Attempted to call a type that did not support it.");
            }
            virtual Expression BuildMetaCall(Expression val, std::vector<Expression> args, Analyzer& a) {
                throw std::runtime_error("Attempted to call a type that did not support it.");
            }
            virtual Expression BuildEQComparison(Expression lhs, Expression rhs, Analyzer& a) {
                throw std::runtime_error("This type cannot be compared.");
            }
            virtual Expression BuildNEComparison(Expression lhs, Expression rhs, Analyzer& a) {
                throw std::runtime_error("This type cannot be compared for inequality.");
            }
            virtual Expression BuildLTComparison(Expression lhs, Expression rhs, Analyzer& a) {
                throw std::runtime_error("This type cannot be compared for inequality.");
            }
            virtual Expression BuildLTEComparison(Expression lhs, Expression rhs, Analyzer& a) {
                throw std::runtime_error("This type cannot be compared for inequality.");
            }
            virtual Expression BuildGTComparison(Expression lhs, Expression rhs, Analyzer& a) {
                throw std::runtime_error("This type cannot be compared for inequality.");
            }
            virtual Expression BuildGTEComparison(Expression lhs, Expression rhs, Analyzer& a) {
                throw std::runtime_error("This type cannot be compared for inequality.");
            }
            
            virtual ConversionRank RankConversionFrom(Type* to, Analyzer& a);
            //Or,
            //And,
            //Xor
            virtual Codegen::Expression* BuildBooleanConversion(Expression val, Analyzer& a) {
                throw std::runtime_error("Could not convert a type to boolean.");
            }
                        
            virtual ~Type() {}
        };     
    }
}