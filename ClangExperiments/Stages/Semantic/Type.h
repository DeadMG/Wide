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
            
            /*Expression BuildValueConstruction(std::vector<Expression> args, Analyzer& a);
            Expression BuildRvalueConstruction(std::vector<Expression> args, Analyzer& a);
            Expression BuildLvalueConstruction(std::vector<Expression> args, Analyzer& a);
            Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, std::vector<Expression> args, Analyzer& a);*/

            Expression BuildValue(Analyzer& a);
            Expression BuildRightShift(Expression rhs, Analyzer& a);
            Expression BuildLeftShift(Expression rhs, Analyzer& a);
            Expression BuildAssignment(Expression rhs, Analyzer& a);
            Expression AccessMember(std::string name, Analyzer& a);
            Expression BuildCall(std::vector<Expression> args, Analyzer& a);
            Expression BuildCall(Expression arg, Analyzer& a);
            Expression BuildCall(Analyzer& a);
            Expression BuildMetaCall(std::vector<Expression> args, Analyzer& a);
            Expression BuildEQComparison(Expression rhs, Analyzer& a);
            Expression BuildNEComparison(Expression rhs, Analyzer& a);
            Expression BuildLTComparison(Expression rhs, Analyzer& a);
            Expression BuildLTEComparison(Expression rhs, Analyzer& a);
            Expression BuildGTComparison(Expression rhs, Analyzer& a);
            Expression BuildGTEComparison(Expression rhs, Analyzer& a);
            Expression BuildDereference(Analyzer& a);
            Expression BuildOr(Expression rhs, Analyzer& a);
            Expression BuildAnd(Expression rhs, Analyzer& a);            
            Expression BuildMultiply(Expression rhs, Analyzer& a);
            Expression BuildPlus(Expression rhs, Analyzer& a);
            Expression BuildIncrement(bool postfix, Analyzer& a);

            Expression PointerAccessMember(std::string name, Analyzer& a);

            Expression AddressOf(Analyzer& a);

            Codegen::Expression* BuildBooleanConversion(Analyzer& a);
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
            
            virtual Expression BuildValueConstruction(Expression arg, Analyzer& a);
            virtual Expression BuildRvalueConstruction(Expression arg, Analyzer& a);
            virtual Expression BuildLvalueConstruction(Expression args, Analyzer& a);
            virtual Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, Expression args, Analyzer& a);

            virtual Expression BuildValueConstruction(Analyzer& a);
            virtual Expression BuildRvalueConstruction(Analyzer& a);
            virtual Expression BuildLvalueConstruction(Analyzer& a);
            virtual Codegen::Expression* BuildInplaceConstruction(Codegen::Expression* mem, Analyzer& a);

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
            virtual Expression BuildNEComparison(Expression lhs, Expression rhs, Analyzer& a);
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
            virtual Expression BuildDereference(Expression obj, Analyzer& a) {
                throw std::runtime_error("This type does not support de-referencing.");
            }
            virtual Expression BuildOr(Expression lhs, Expression rhs, Analyzer& a) {
                if (IsReference())
                    return IsReference()->BuildOr(lhs, rhs, a);
                throw std::runtime_error("Attempted to use operator| on a type that did not support it.");
            }
            virtual Expression BuildAnd(Expression lhs, Expression rhs, Analyzer& a) {
                if (IsReference())
                    return IsReference()->BuildAnd(lhs, rhs, a);
                throw std::runtime_error("Attempted to use operator| on a type that did not support it.");
            }
            
            virtual Expression BuildMultiply(Expression lhs, Expression rhs, Analyzer& a) {
                if (IsReference())
                    return IsReference()->BuildMultiply(lhs, rhs, a);
                throw std::runtime_error("Attempted to multiply a type that did not support it.");
            }
            virtual Expression BuildPlus(Expression lhs, Expression rhs, Analyzer& a) {
                if (IsReference())
                    return IsReference()->BuildPlus(lhs, rhs, a);
                throw std::runtime_error("Attempted to add a type that did not support it.");
            }
            virtual Expression BuildIncrement(Expression obj, bool postfix, Analyzer& a) {
                if (IsReference())
                    return IsReference()->BuildIncrement(obj, postfix, a);
                throw std::runtime_error("Attempted to increment a type that did not support it.");
            }

            virtual Expression PointerAccessMember(Expression obj, std::string name, Analyzer& a) {
                obj = obj.t->BuildDereference(obj, a);
                return obj.t->AccessMember(obj, std::move(name), a);
            }

            virtual Expression AddressOf(Expression obj, Analyzer& a);

            virtual ConversionRank RankConversionFrom(Type* to, Analyzer& a);
            //Or,
            //And,
            //Xor
            virtual Codegen::Expression* BuildBooleanConversion(Expression val, Analyzer& a) {
                throw std::runtime_error("Could not convert a type to boolean.");
            }
            virtual std::size_t size(Analyzer& a) { throw std::runtime_error("Attempted to size a meta-type that does not have a run-time size."); }
            virtual std::size_t alignment(Analyzer& a) { throw std::runtime_error("Attempted to align a meta-type that does not have a run-time alignment."); }
                        
            virtual ~Type() {}
        };     
    }
}