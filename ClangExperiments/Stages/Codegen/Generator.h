#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include "../../Util/MemoryArena.h"
#include "Expression.h"
#include "../LLVMOptions.h"
#include "../ClangOptions.h"
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
    namespace Codegen {
        class Function;
        class Generator {
            std::deque<Function*> functions;
            Wide::Memory::Arena arena;

            void EmitCode();
            const Options::LLVM& llvmopts;
            const Options::Clang& clangopts;
            std::unordered_map<llvm::Function*, Function*> funcs;
            std::unordered_set<llvm::Type*> eliminate_types;

        public:            
            bool IsEliminateType(llvm::Type*);
            void AddEliminateType(llvm::Type*);

            void TieFunction(llvm::Function*, Function*);
            Function* FromLLVMFunc(llvm::Function*);

            llvm::LLVMContext context;
            llvm::Module main;

            Generator(const Options::LLVM&, const Options::Clang&);

            Function* CreateFunction(std::function<llvm::Type*(llvm::Module*)>, std::string, bool trampoline = false);

            Variable* CreateVariable(std::function<llvm::Type*(llvm::Module*)>);
            FunctionCall* CreateFunctionCall(Expression*, std::vector<Expression*>, std::function<llvm::Type*(llvm::Module*)> = std::function<llvm::Type*(llvm::Module*)>());
            StringExpression* CreateStringExpression(std::string);
            NamedGlobalVariable* CreateGlobalVariable(std::string);
            StoreExpression* CreateStore(Expression*, Expression*);
            LoadExpression* CreateLoad(Expression*);
            ReturnStatement* CreateReturn();
            ReturnStatement* CreateReturn(Expression*);
            FunctionValue* CreateFunctionValue(std::string);
            Int8Expression* CreateInt8Expression(char val);
            ChainExpression* CreateChainExpression(Statement*, Expression*);
            FieldExpression* CreateFieldExpression(Expression*, unsigned);
            ParamExpression* CreateParameterExpression(unsigned);
            ParamExpression* CreateParameterExpression(std::function<unsigned()>);
            IfStatement* CreateIfStatement(Expression*, Statement*, Statement*);
            ChainStatement* CreateChainStatement(Statement*, Statement*);
            TruncateExpression* CreateTruncate(Expression*, std::function<llvm::Type*(llvm::Module*)>);
            WhileStatement* CreateWhile(Expression*, Statement*);
            NullExpression* CreateNull(std::function<llvm::Type*(llvm::Module*)> type);
            IntegralLeftShiftExpression* CreateLeftShift(Expression*, Expression*);
            IntegralRightShiftExpression* CreateRightShift(Expression*, Expression*);

            void operator()();
        };
    }
}