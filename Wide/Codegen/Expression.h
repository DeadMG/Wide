#pragma once

#include <Wide/Codegen/Statement.h>
#include <vector>
#include <functional>
#include <string>

namespace llvm {
    class Type;
};
namespace clang {
    class VarDecl;
}
namespace Wide {
    namespace ClangUtil {
        class ClangTU;
    }
    namespace Semantic {
        struct Type;
        struct Variable;
    }
    namespace LLVMCodegen {
        struct Expression : Statement {
        public:
            Expression() : val(nullptr) {}

            void Build(llvm::IRBuilder<>& bb, Generator& g);
            llvm::Value* GetValue(llvm::IRBuilder<>& bb, Generator& g);
        protected:
            llvm::Value* val;
            virtual llvm::Value* ComputeValue(llvm::IRBuilder<>&, Generator& g) = 0;
        };

		class Variable : public Expression, public Codegen::Variable {
            std::function<llvm::Type*(llvm::Module*)> t;
            unsigned align;
        public:
            Variable(std::function<llvm::Type*(llvm::Module*)> ty, unsigned alignment)
                : t(std::move(ty)), align(alignment) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>&, Generator& g);
        };

		class FunctionCall : public Expression, public Codegen::FunctionCall {
            std::vector<LLVMCodegen::Expression*> arguments;
            LLVMCodegen::Expression* object;
            std::function<llvm::Type*(llvm::Module*)> CastTy;
        public:
            LLVMCodegen::Expression* GetCallee() { return object; }
            FunctionCall(LLVMCodegen::Expression* obj, std::vector<LLVMCodegen::Expression*> args, std::function<llvm::Type*(llvm::Module*)>);
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };
        
		class FunctionValue : public Expression, public Codegen::FunctionValue {
            std::string mangled_name;
        public:
            std::string GetMangledName() {
                return mangled_name;
            }
            FunctionValue(std::string name);
            llvm::Value* ComputeValue(llvm::IRBuilder<>&, Generator& g);
        };

		class LoadExpression : public Expression, public Codegen::LoadExpression {
            LLVMCodegen::Expression* obj;
        public:
            LoadExpression(LLVMCodegen::Expression* o)
                : obj(o) {}

            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };
        
		class ChainExpression : public Expression, public Codegen::ChainExpression {
            LLVMCodegen::Statement* s;
            LLVMCodegen::Expression* next;
        public:
            ChainExpression(LLVMCodegen::Statement* stat, LLVMCodegen::Expression* e)
                : s(stat), next(e) {}

            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };
        
		class StringExpression : public Expression, public Codegen::StringExpression {
            std::string value;
        public:
            StringExpression(std::string expr)
                : value(std::move(expr)) {}

            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);

            
            std::string GetContents() {
                return value;
            }
        };

		class NamedGlobalVariable : public Expression, public Codegen::NamedGlobalVariable {
            std::string mangled;
        public:
            NamedGlobalVariable(std::string mangledname)
                : mangled(std::move(mangledname)) {}

            llvm::Value* ComputeValue(llvm::IRBuilder<>&, Generator& g);
        };              

		class StoreExpression : public Expression, public Codegen::StoreExpression {
            LLVMCodegen::Expression* obj;
            LLVMCodegen::Expression* val;
        public:
            StoreExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r)
                : obj(l), val(r) {}
            
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

		class IntegralExpression : public Expression, public Codegen::IntegralExpression {
            std::function<llvm::Type*(llvm::Module*)> type;
        public:
            IntegralExpression(unsigned long long val, bool s, std::function<llvm::Type*(llvm::Module*)> t)
                : value(val), sign(s), type(std::move(t)) {}

			unsigned long long GetValue() { return value; }
			bool GetSign() { return sign; }

            unsigned long long value;
            bool sign;

            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };


		class FieldExpression : public Expression, public Codegen::FieldExpression {
            std::function<unsigned()> fieldnum;
            LLVMCodegen::Expression* obj;
        public:
            FieldExpression(std::function<unsigned()> f, LLVMCodegen::Expression* o)
                : fieldnum(f), obj(o) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

		class ParamExpression : public Expression, public Codegen::ParamExpression {
            std::function<unsigned()> param;
        public:
            ParamExpression(std::function<unsigned()> p)
                : param(std::move(p)) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

        class TruncateExpression : public Expression, public Codegen::TruncateExpression {
            LLVMCodegen::Expression* val;
            std::function<llvm::Type*(llvm::Module*)> ty;
        public:
            TruncateExpression(LLVMCodegen::Expression* e, std::function<llvm::Type*(llvm::Module*)> type)
                : val(e), ty(std::move(type)) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

        class NullExpression : public Expression, public Codegen::NullExpression {
            std::function<llvm::Type*(llvm::Module*)> ty;
        public:
            NullExpression(std::function<llvm::Type*(llvm::Module*)> type)
                : ty(std::move(type)) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

        class IntegralLeftShiftExpression : public Expression, public Codegen::IntegralLeftShiftExpression {
            LLVMCodegen::Expression* lhs;
            LLVMCodegen::Expression* rhs;
        public:
            IntegralLeftShiftExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r)
                : lhs(l), rhs(r) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

        class IntegralRightShiftExpression : public Expression, public Codegen::IntegralRightShiftExpression {
            LLVMCodegen::Expression* lhs;
            LLVMCodegen::Expression* rhs;
            bool is_signed;
        public:
            IntegralRightShiftExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r, bool s)
                : lhs(l), rhs(r), is_signed(s) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

        class IntegralLessThan : public Expression, public Codegen::IntegralLessThan {
            LLVMCodegen::Expression* lhs;
            LLVMCodegen::Expression* rhs;
            bool sign;
        public:
            IntegralLessThan(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r, bool sign)
                : lhs(l), rhs(r), sign(sign) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

        class ZExt : public Expression, public Codegen::ZExt {
            LLVMCodegen::Expression* from;
            std::function<llvm::Type*(llvm::Module*)> to;
        public:
            ZExt(LLVMCodegen::Expression* f, std::function<llvm::Type*(llvm::Module*)> ty)
                : from(f), to(std::move(ty)) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

        class SExt : public Expression, public Codegen:: SExt {
            LLVMCodegen::Expression* from;
            std::function<llvm::Type*(llvm::Module*)> to;
        public:
            SExt(LLVMCodegen::Expression* f, std::function<llvm::Type*(llvm::Module*)> ty)
                : from(f), to(std::move(ty)) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

        class NegateExpression : public Expression, public Codegen::NegateExpression {
            LLVMCodegen::Expression* expr;
        public:
            NegateExpression(LLVMCodegen::Expression* ex)
                : expr(ex) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

        class OrExpression : public Expression, public Codegen::OrExpression {
            LLVMCodegen::Expression* lhs;
            LLVMCodegen::Expression* rhs;
        public:
            OrExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r)
                : lhs(l), rhs(r) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };
        class AndExpression : public Expression, public Codegen::AndExpression {
            LLVMCodegen::Expression* lhs;
            LLVMCodegen::Expression* rhs;
        public:
            AndExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r)
                : lhs(l), rhs(r) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

        class EqualityExpression : public Expression, public Codegen::EqualityExpression {
            LLVMCodegen::Expression* lhs;
            LLVMCodegen::Expression* rhs;
        public:
            EqualityExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r)
                : lhs(l), rhs(r) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };
        class PlusExpression : public Expression, public Codegen::PlusExpression  {
            LLVMCodegen::Expression* lhs;
            LLVMCodegen::Expression* rhs;
        public:
            PlusExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r)
                : lhs(l), rhs(r) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };
        class MultiplyExpression : public Expression, public Codegen::MultiplyExpression  {
            LLVMCodegen::Expression* lhs;
            LLVMCodegen::Expression* rhs;
        public:
            MultiplyExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r)
                : lhs(l), rhs(r) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

        class IsNullExpression : public Expression, public Codegen::IsNullExpression {
            LLVMCodegen::Expression* ptr;
        public:
            IsNullExpression(LLVMCodegen::Expression* p)
                : ptr(p) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

		class XorExpression : public Expression, public Codegen::XorExpression {
			LLVMCodegen::Expression* lhs;
			LLVMCodegen::Expression* rhs;
		public:
			XorExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r) : lhs(l), rhs(r) {}
			llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
		};

		class SubExpression : public Expression, public Codegen::SubExpression {
			LLVMCodegen::Expression* lhs;
			LLVMCodegen::Expression* rhs;
		public:
			SubExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r) : lhs(l), rhs(r) {}
			llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
		};

		class ModExpression : public Expression, public Codegen::ModExpression {
			LLVMCodegen::Expression* lhs;
			LLVMCodegen::Expression* rhs;
			bool is_signed;
		public:
			ModExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r, bool is_sign) : lhs(l), rhs(r), is_signed(is_sign){}
			llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
		};

		class DivExpression : public Expression, public Codegen::DivExpression {
			LLVMCodegen::Expression* lhs;
			LLVMCodegen::Expression* rhs;
			bool is_signed;
		public:
			DivExpression(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r, bool is_sign) : lhs(l), rhs(r), is_signed(is_sign) {}
			llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
		};
		
		class FPExtension : public Expression, public Codegen::FPExtension {
            LLVMCodegen::Expression* from;
            std::function<llvm::Type*(llvm::Module*)> to;
        public:
			FPExtension(LLVMCodegen::Expression* f, std::function<llvm::Type*(llvm::Module*)> ty)
                : from(f), to(std::move(ty)) {}
            llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
        };

		class FPDiv : public Expression, public Codegen::FPDiv {
			LLVMCodegen::Expression* lhs;
			LLVMCodegen::Expression* rhs;
		public:
			FPDiv(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r) : lhs(l), rhs(r) {}
			llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
		};

		class FPMod : public Expression, public Codegen::FPMod {
			LLVMCodegen::Expression* lhs;
			LLVMCodegen::Expression* rhs;
		public:
			FPMod(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r) : lhs(l), rhs(r) {}
			llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
		};

		class FPLT : public Expression, public Codegen::FPLessThan {
			LLVMCodegen::Expression* lhs;
			LLVMCodegen::Expression* rhs;
		public:
			FPLT(LLVMCodegen::Expression* l, LLVMCodegen::Expression* r) : lhs(l), rhs(r) {}
			llvm::Value* ComputeValue(llvm::IRBuilder<>& builder, Generator& g);
		};
    }
}