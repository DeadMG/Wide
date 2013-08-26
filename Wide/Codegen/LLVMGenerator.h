#pragma once
#include <Wide/Util/MemoryArena.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Codegen/LLVMOptions.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Codegen/Function.h>
#include <unordered_map>
#include <deque>
#include <vector>
#include <unordered_set>
#include <string>
#include <functional>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>
#pragma warning(pop)

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
            std::deque<Function*> functions;
            Wide::Memory::Arena arena;

            std::string outputfile;
            std::string triple;

            void EmitCode();
            const Options::LLVM& llvmopts;
            std::unordered_map<llvm::Function*, Function*> funcs;
            std::unordered_set<llvm::Type*> eliminate_types;
            std::vector<std::function<void(llvm::Module*)>> tus;

        public:            
            bool IsEliminateType(llvm::Type*);
            void AddEliminateType(llvm::Type*);
            void TieFunction(llvm::Function*, Function*);
            Function* FromLLVMFunc(llvm::Function*);

            llvm::LLVMContext context;
            llvm::Module main;

            Generator(const Options::LLVM&, std::string outputfile, std::string triple);

            Function* CreateFunction(std::function<llvm::Type*(llvm::Module*)>, std::string, Semantic::Function* debug, bool trampoline = false);

            Variable* CreateVariable(std::function<llvm::Type*(llvm::Module*)>, unsigned alignment) final;
            FunctionCall* CreateFunctionCall(Codegen::Expression*, std::vector<Codegen::Expression*>, std::function<llvm::Type*(llvm::Module*)> = std::function<llvm::Type*(llvm::Module*)>());
            StringExpression* CreateStringExpression(std::string);
            NamedGlobalVariable* CreateGlobalVariable(std::string);
            StoreExpression* CreateStore(Codegen::Expression*, Codegen::Expression*);
            LoadExpression* CreateLoad(Codegen::Expression*);
            ReturnStatement* CreateReturn();
            ReturnStatement* CreateReturn(Codegen::Expression*);
            FunctionValue* CreateFunctionValue(std::string);
            IntegralExpression* CreateIntegralExpression(uint64_t val, bool is_signed, std::function<llvm::Type*(llvm::Module*)> ty);
            ChainExpression* CreateChainExpression(Codegen::Statement*, Codegen::Expression*);
            FieldExpression* CreateFieldExpression(Codegen::Expression*, unsigned);
            FieldExpression* CreateFieldExpression(Codegen::Expression*, std::function<unsigned()>);
            ParamExpression* CreateParameterExpression(unsigned);
            ParamExpression* CreateParameterExpression(std::function<unsigned()>);
            IfStatement* CreateIfStatement(Codegen::Expression*, Codegen::Statement*, Codegen::Statement*);
            ChainStatement* CreateChainStatement(Codegen::Statement*, Codegen::Statement*);
            TruncateExpression* CreateTruncate(Codegen::Expression*, std::function<llvm::Type*(llvm::Module*)>);
            WhileStatement* CreateWhile(Codegen::Expression*, Codegen::Statement*);
            NullExpression* CreateNull(std::function<llvm::Type*(llvm::Module*)> type);
            IntegralLeftShiftExpression* CreateLeftShift(Codegen::Expression*, Codegen::Expression*);
            IntegralRightShiftExpression* CreateRightShift(Codegen::Expression*, Codegen::Expression*, bool);
            IntegralLessThan* CreateLT(Codegen::Expression* lhs, Codegen::Expression* rhs, bool);
            ZExt* CreateZeroExtension(Codegen::Expression* val, std::function<llvm::Type*(llvm::Module*)> to);
            NegateExpression* CreateNegateExpression(Codegen::Expression* val);
            OrExpression* CreateOrExpression(Codegen::Expression* lhs, Codegen::Expression* rhs);
            EqualityExpression* CreateEqualityExpression(Codegen::Expression* lhs, Codegen::Expression* rhs);
            PlusExpression* CreatePlusExpression(Codegen::Expression* lhs, Codegen::Expression* rhs);
            MultiplyExpression* CreateMultiplyExpression(Codegen::Expression* lhs, Codegen::Expression* rhs);
            AndExpression* CreateAndExpression(Codegen::Expression* lhs, Codegen::Expression* rhs);
            SExt* CreateSignedExtension(Codegen::Expression* val, std::function<llvm::Type*(llvm::Module*)> to);
            IsNullExpression* CreateIsNullExpression(Codegen::Expression* val);
            SubExpression* CreateSubExpression(Codegen::Expression* l, Codegen::Expression* r);
            XorExpression* CreateXorExpression(Codegen::Expression* l, Codegen::Expression* r);
            ModExpression* CreateModExpression(Codegen::Expression* l, Codegen::Expression* r, bool is_signed);
            DivExpression* CreateDivExpression(Codegen::Expression* l, Codegen::Expression* r, bool is_signed);
            FPExtension* CreateFPExtension(Codegen::Expression* l, std::function<llvm::Type*(llvm::Module*)> r);
            FPMod* CreateFPMod(Codegen::Expression* r, Codegen::Expression* l);
            FPDiv* CreateFPDiv(Codegen::Expression* r, Codegen::Expression* l);
            FPLT* CreateFPLT(Codegen::Expression* r, Codegen::Expression* l);
            
            llvm::DataLayout GetDataLayout();
            void AddClangTU(std::function<void(llvm::Module* m)>);
            llvm::LLVMContext& GetContext();
            std::size_t Wide::Codegen::Generator::GetInt8AllocSize();

            void operator()();
        };
    }
}