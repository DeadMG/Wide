#include <Wide/Codegen/Generator.h>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#pragma warning(pop)

#define CATCH_CONFIG_MAIN
#include <Wide/Util/Catch.h>

namespace Wide {
	namespace Codegen {
		class MockFunction : public Function {
		public:
			void AddStatement(Codegen::Statement*) {}
		};
		class MockIntegralExpression : public IntegralExpression {
			unsigned long long value;
			bool sign;
		public:
			MockIntegralExpression(unsigned long long val, bool s)
				: value(val), sign(s) {}
			unsigned long long GetValue() { return value; }
			bool GetSign() { return sign; }
		};
		class MockFunctionValue : public FunctionValue {
			std::string name;
		public:
			MockFunctionValue(std::string nam)
				: name(std::move(nam)) {}
			std::string GetMangledName() { return name; }
		};
		class MockStringExpression : public StringExpression {
			std::string val;
		public:
			MockStringExpression(std::string value)
				: val(std::move(value)) {}
			std::string GetContents() { return val; }
		};
        class MockCodeGenerator : public Generator {
        public:
			MockCodeGenerator(llvm::DataLayout data)
				: layout(data) {}
			llvm::LLVMContext con;
			llvm::DataLayout layout;

			virtual llvm::LLVMContext& GetContext() { return con; }
			virtual llvm::DataLayout GetDataLayout() { return layout; }
			virtual void AddEliminateType(llvm::Type* t) {}
			virtual void AddClangTU(std::function<void(llvm::Module*)>) {}
			virtual std::size_t GetInt8AllocSize() { return 1; }

			virtual Function* CreateFunction(std::function<llvm::Type*(llvm::Module*)>, std::string, Semantic::Function* debug, bool trampoline = false) { return new MockFunction; }
        	virtual Variable* CreateVariable(std::function<llvm::Type*(llvm::Module*)>, unsigned alignment) { return nullptr; }
            virtual FunctionCall* CreateFunctionCall(Expression*, std::vector<Expression*>, std::function<llvm::Type*(llvm::Module*)> = std::function<llvm::Type*(llvm::Module*)>()) { return nullptr; }
			virtual StringExpression* CreateStringExpression(std::string val) { return new MockStringExpression(std::move(val)); }
			virtual NamedGlobalVariable* CreateGlobalVariable(std::string) { return nullptr; }
            virtual StoreExpression* CreateStore(Expression*, Expression*) { return nullptr; }
            virtual LoadExpression* CreateLoad(Expression*) { return nullptr; }
            virtual ReturnStatement* CreateReturn() { return nullptr; }
            virtual ReturnStatement* CreateReturn(Expression*) { return nullptr; }
			virtual FunctionValue* CreateFunctionValue(std::string name) { return new MockFunctionValue(std::move(name)); }
			virtual IntegralExpression* CreateIntegralExpression(unsigned long long val, bool is_signed, std::function<llvm::Type*(llvm::Module*)> ty) { return new MockIntegralExpression(val, is_signed); }
            virtual ChainExpression* CreateChainExpression(Statement*, Expression*) { return nullptr; }
            virtual FieldExpression* CreateFieldExpression(Expression*, unsigned) { return nullptr; }
            virtual FieldExpression* CreateFieldExpression(Expression*, std::function<unsigned()>) { return nullptr; }
            virtual ParamExpression* CreateParameterExpression(unsigned) { return nullptr; }
            virtual ParamExpression* CreateParameterExpression(std::function<unsigned()>) { return nullptr; }
            virtual IfStatement* CreateIfStatement(Expression*, Statement*, Statement*) { return nullptr; }
            virtual ChainStatement* CreateChainStatement(Statement*, Statement*) { return nullptr; }
            virtual TruncateExpression* CreateTruncate(Expression*, std::function<llvm::Type*(llvm::Module*)>) { return nullptr; }
            virtual WhileStatement* CreateWhile(Expression*, Statement*) { return nullptr; }
            virtual NullExpression* CreateNull(std::function<llvm::Type*(llvm::Module*)> type) { return nullptr; }
            virtual IntegralLeftShiftExpression* CreateLeftShift(Expression*, Expression*) { return nullptr; }
            virtual IntegralRightShiftExpression* CreateRightShift(Expression*, Expression*, bool) { return nullptr; }
            virtual IntegralLessThan* CreateLT(Expression* lhs, Expression* rhs, bool) { return nullptr; }
            virtual ZExt* CreateZeroExtension(Expression* val, std::function<llvm::Type*(llvm::Module*)> to) { return nullptr; }
            virtual NegateExpression* CreateNegateExpression(Expression* val) { return nullptr; }
            virtual OrExpression* CreateOrExpression(Expression* lhs, Expression* rhs) { return nullptr; }
            virtual EqualityExpression* CreateEqualityExpression(Expression* lhs, Expression* rhs) { return nullptr; }
            virtual PlusExpression* CreatePlusExpression(Expression* lhs, Expression* rhs) { return nullptr; }
            virtual MultiplyExpression* CreateMultiplyExpression(Expression* lhs, Expression* rhs) { return nullptr; }
            virtual AndExpression* CreateAndExpression(Expression* lhs, Expression* rhs) { return nullptr; }
            virtual SExt* CreateSignedExtension(Expression* val, std::function<llvm::Type*(llvm::Module*)> to) { return nullptr; }
            virtual IsNullExpression* CreateIsNullExpression(Expression* val) { return nullptr; }
        	virtual SubExpression* CreateSubExpression(Expression* l, Expression* r) { return nullptr; }
        	virtual XorExpression* CreateXorExpression(Expression* l, Expression* r) { return nullptr; }
        	virtual ModExpression* CreateModExpression(Expression* l, Expression* r, bool is_signed) { return nullptr; }
        	virtual DivExpression* CreateDivExpression(Expression* l, Expression* r, bool is_signed) { return nullptr; }        
        };
	}
}

TEST_CASE("", "") {
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();
	std::string triple = "i686-pc-mingw32";
    std::unique_ptr<llvm::TargetMachine> targetmachine;
    std::string err;
    const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(triple, err);
    llvm::TargetOptions targetopts;
    targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(triple, llvm::Triple(triple).getArchName(), "", targetopts));
	Wide::Codegen::MockCodeGenerator mockcodegen(*targetmachine->getDataLayout());
	REQUIRE(true);
}