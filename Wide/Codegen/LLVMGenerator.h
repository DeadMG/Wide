#pragma once
#include <Wide/Util/Memory/MemoryArena.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Codegen/LLVMOptions.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Codegen/Function.h>
#include <unordered_map>
#include <list>
#include <vector>
#include <unordered_set>
#include <string>
#include <functional>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#pragma warning(pop)

#pragma warning(disable : 4373)

namespace llvm {
    class Type;
}
namespace Wide {
    namespace Semantic {
        class Function;
    }
    namespace LLVMCodegen {
        class Function;
        class Generator : public Codegen::Generator {
            std::list<Function*> functions;
            Wide::Memory::Arena arena;

            std::function<void(std::unique_ptr<llvm::Module>)> func;
            std::string triple;

            void EmitCode();
            const Options::LLVM& llvmopts;
            std::unordered_map<llvm::Function*, Function*> funcs;
            std::unordered_map<std::string, Function*> named_funcs;
            std::unordered_set<llvm::Type*> eliminate_types;
            std::vector<std::function<void(llvm::Module*)>> tus;

        public:            
            bool IsEliminateType(llvm::Type*);
            void AddEliminateType(llvm::Type*) override final;
            void TieFunction(llvm::Function*, Function*);
            Function* FromLLVMFunc(llvm::Function*);
            Function* GetFunctionByName(std::string name);

            llvm::LLVMContext context;
        private:
            std::unique_ptr<llvm::Module> main;
            std::string layout;
        public:

            Generator(const Options::LLVM&, std::string triple, std::function<void(std::unique_ptr<llvm::Module>)> action);

            Function* CreateFunction(std::function<llvm::Type*(llvm::Module*)>, std::string, Semantic::Function* debug, bool trampoline = false) override final;
            Deferred* CreateDeferredStatement(const std::function<Codegen::Statement*()>) override final;
            DeferredExpr* CreateDeferredExpression(const std::function<Codegen::Expression*()>) override final;
            Variable* CreateVariable(std::function<llvm::Type*(llvm::Module*)>, unsigned alignment) override final;
            FunctionCall* CreateFunctionCall(Codegen::Expression*, std::vector<Codegen::Expression*>, std::function<llvm::Type*(llvm::Module*)> = std::function<llvm::Type*(llvm::Module*)>()) override final;
            StringExpression* CreateStringExpression(std::string) override final;
            NamedGlobalVariable* CreateGlobalVariable(std::string) override final;
            StoreExpression* CreateStore(Codegen::Expression*, Codegen::Expression*) override final;
            LoadExpression* CreateLoad(Codegen::Expression*) override final;
            ReturnStatement* CreateReturn() override final; 

            // Const parameter or compiler bug
            IfStatement* CreateIfStatement(const std::function<Codegen::Expression*()>, Codegen::Statement*, Codegen::Statement*) override final;
            WhileStatement* CreateWhile(const std::function<Codegen::Expression*()>) override final;
            ReturnStatement* CreateReturn(const std::function<Codegen::Expression*()>) override final;
            ReturnStatement* CreateReturn(Codegen::Expression* e) override final {
                return CreateReturn([=] { return e; });
            }
            FunctionValue* CreateFunctionValue(std::string) override final;
            IntegralExpression* CreateIntegralExpression(std::uint64_t val, bool is_signed, std::function<llvm::Type*(llvm::Module*)> ty) override final;
            ChainExpression* CreateChainExpression(Codegen::Statement*, Codegen::Expression*) override final;
            FieldExpression* CreateFieldExpression(Codegen::Expression*, unsigned) override final;
            FieldExpression* CreateFieldExpression(Codegen::Expression*, std::function<unsigned()>) override final;
            ParamExpression* CreateParameterExpression(unsigned) override final;
            ParamExpression* CreateParameterExpression(std::function<unsigned()>) override final; 
            IfStatement* CreateIfStatement(Codegen::Expression* expr, Codegen::Statement* t, Codegen::Statement* f) override final {
                return CreateIfStatement([=] { return expr; }, t, f);
            }
            Nop* CreateNop();
            ChainStatement* CreateChainStatement(Codegen::Statement*, Codegen::Statement*) override final;
            TruncateExpression* CreateTruncate(Codegen::Expression*, std::function<llvm::Type*(llvm::Module*)>) override final;
            WhileStatement* CreateWhile(Codegen::Expression* e) override final {
                return CreateWhile([=]{ return e; });
            }
            NullExpression* CreateNull(std::function<llvm::Type*(llvm::Module*)> type) override final;
            IntegralLeftShiftExpression* CreateLeftShift(Codegen::Expression*, Codegen::Expression*) override final;
            IntegralRightShiftExpression* CreateRightShift(Codegen::Expression*, Codegen::Expression*, bool) override final;
            IntegralLessThan* CreateLT(Codegen::Expression* lhs, Codegen::Expression* rhs, bool) override final;
            ZExt* CreateZeroExtension(Codegen::Expression* val, std::function<llvm::Type*(llvm::Module*)> to) override final;
            NegateExpression* CreateNegateExpression(Codegen::Expression* val) override final;
            OrExpression* CreateOrExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) override final;
            EqualityExpression* CreateEqualityExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) override final;
            PlusExpression* CreatePlusExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) override final;
            MultiplyExpression* CreateMultiplyExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) override final;
            AndExpression* CreateAndExpression(Codegen::Expression* lhs, Codegen::Expression* rhs) override final;
            SExt* CreateSignedExtension(Codegen::Expression* val, std::function<llvm::Type*(llvm::Module*)> to) override final;
            IsNullExpression* CreateIsNullExpression(Codegen::Expression* val) override final;
            SubExpression* CreateSubExpression(Codegen::Expression* l, Codegen::Expression* r) override final;
            XorExpression* CreateXorExpression(Codegen::Expression* l, Codegen::Expression* r) override final;
            ModExpression* CreateModExpression(Codegen::Expression* l, Codegen::Expression* r, bool is_signed) override final;
            DivExpression* CreateDivExpression(Codegen::Expression* l, Codegen::Expression* r, bool is_signed) override final;
            FPExtension* CreateFPExtension(Codegen::Expression* l, std::function<llvm::Type*(llvm::Module*)> r) override final;
            FPMod* CreateFPMod(Codegen::Expression* r, Codegen::Expression* l) override final;
            FPDiv* CreateFPDiv(Codegen::Expression* r, Codegen::Expression* l) override final;
            FPLT* CreateFPLT(Codegen::Expression* r, Codegen::Expression* l) override final;
            ContinueStatement* CreateContinue(Codegen::WhileStatement* s) override final;
            BreakStatement* CreateBreak(Codegen::WhileStatement* s) override final;
            LifetimeEnd* CreateLifetimeEnd(Codegen::Expression* e) override final;
            llvm::DataLayout GetDataLayout() override final;
            void AddClangTU(std::function<void(llvm::Module* m)>) override final;
            llvm::LLVMContext& GetContext() override final;
            std::size_t GetInt8AllocSize() override final;

            void operator()();
        };
    }
}