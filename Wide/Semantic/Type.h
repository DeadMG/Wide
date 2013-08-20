#pragma once

#include <Wide/Util/Ranges/Optional.h>
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
        class Expression;
        class Generator;
    }
    namespace AST {
        struct DeclContext;
    }
	namespace Lexer {
		enum class TokenType : int;
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
            
            Expression BuildValue(Analyzer& a);
            Expression BuildRightShift(Expression rhs, Analyzer& a);
            Expression BuildLeftShift(Expression rhs, Analyzer& a);
            Expression BuildAssignment(Expression rhs, Analyzer& a);
            Wide::Util::optional<Expression> AccessMember(std::string name, Analyzer& a);
            Expression BuildCall(std::vector<Expression> args, Analyzer& a);
			Expression BuildCall(Expression lhs, Expression rhs, Analyzer& a);
            Expression BuildCall(Expression arg, Analyzer& a);
            Expression BuildCall(Analyzer& a);
            Expression BuildMetaCall(std::vector<Expression> args, Analyzer& a);
            Expression BuildDereference(Analyzer& a);
            Expression BuildIncrement(bool postfix, Analyzer& a);
			Expression BuildNegate(Analyzer& a);
			
			Expression BuildBinaryExpression(Expression rhs, Lexer::TokenType type, Analyzer& a);

            Wide::Util::optional<Expression> PointerAccessMember(std::string name, Analyzer& a);

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
			virtual bool IsReference(Type* to) {
				return false;
			}
            virtual bool IsReference() {
                return false;
            } 
			virtual Type* Decay() {
                return this;
            }

            virtual AST::DeclContext* GetDeclContext() {
				if (IsReference())
					return Decay()->GetDeclContext();
                return nullptr;
            }
            virtual bool IsComplexType() { return false; }
            virtual clang::QualType GetClangType(ClangUtil::ClangTU& TU, Analyzer& a);
            virtual std::function<llvm::Type*(llvm::Module*)> GetLLVMType(Analyzer& a) {
                throw std::runtime_error("This type has no LLVM counterpart.");
            }

            virtual std::size_t size(Analyzer& a) { throw std::runtime_error("Attempted to size a type that does not have a run-time size."); }
            virtual std::size_t alignment(Analyzer& a) { throw std::runtime_error("Attempted to align a type that does not have a run-time alignment."); }


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


            virtual Expression BuildValue(Expression lhs, Analyzer& a);
            virtual Expression BuildAssignment(Expression lhs, Expression rhs, Analyzer& a) {
                if (IsReference())
					return Decay()->BuildAssignment(lhs, rhs, a);
				throw std::runtime_error("Attempted to use operator= on a type that did not support it.");
            }            
			virtual Wide::Util::optional<Expression> AccessMember(Expression, std::string name, Analyzer& a);
			virtual Wide::Util::optional<Expression> AccessMember(Expression e, Lexer::TokenType type, Analyzer& a) {
				if (IsReference())
					return Decay()->AccessMember(e, type, a);
				return Wide::Util::none;
			}
            virtual Expression BuildCall(Expression val, std::vector<Expression> args, Analyzer& a) {
				if (IsReference())
					return Decay()->BuildCall(val, std::move(args), a);
                throw std::runtime_error("Attempted to call a type that did not support it.");
            }
            virtual Expression BuildMetaCall(Expression val, std::vector<Expression> args, Analyzer& a) {
                throw std::runtime_error("Attempted to call a type that did not support it.");
            }
			virtual ConversionRank RankConversionFrom(Type* to, Analyzer& a);
            virtual Codegen::Expression* BuildBooleanConversion(Expression val, Analyzer& a) {
				if (IsReference())
					return Decay()->BuildBooleanConversion(val, a);
                throw std::runtime_error("Could not convert a type to boolean.");
            }
			
			virtual Expression BuildNegate(Expression val, Analyzer& a);
            virtual Expression BuildIncrement(Expression obj, bool postfix, Analyzer& a) {
                if (IsReference())
                    return Decay()->BuildIncrement(obj, postfix, a);
                throw std::runtime_error("Attempted to increment a type that did not support it.");
            }	
            virtual Expression BuildDereference(Expression obj, Analyzer& a) {
				if (IsReference())
					return Decay()->BuildDereference(obj, a);
                throw std::runtime_error("This type does not support de-referencing.");
            }
            virtual Wide::Util::optional<Expression> PointerAccessMember(Expression obj, std::string name, Analyzer& a) {
				if (IsReference())
					return Decay()->PointerAccessMember(obj, std::move(name), a);
                obj = obj.t->BuildDereference(obj, a);
                return obj.t->AccessMember(obj, std::move(name), a);
            }
            virtual Expression AddressOf(Expression obj, Analyzer& a);

			virtual Expression BuildBinaryExpression(Expression lhs, Expression rhs, Lexer::TokenType type, Analyzer& a);
						                        
            virtual ~Type() {}
        };     
    }
}