#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include <Wide/Util/MemoryArena.h>
#include <Wide/Codegen/Expression.h>
#include <Wide/Codegen/LLVMOptions.h>
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
    namespace Codegen {
        class Function;
        class Generator {
            std::deque<Function*> functions;
            Wide::Memory::Arena arena;

            std::string outputfile;
            std::string triple;

            void EmitCode();
            const Options::LLVM& llvmopts;
            std::unordered_map<llvm::Function*, Function*> funcs;
            std::unordered_set<llvm::Type*> eliminate_types;

        public:            
            bool IsEliminateType(llvm::Type*);
            void AddEliminateType(llvm::Type*);
            void TieFunction(llvm::Function*, Function*);
            Function* FromLLVMFunc(llvm::Function*);

            llvm::LLVMContext context;
            llvm::Module main;

            Generator(const Options::LLVM&, std::string outputfile, std::string triple);

            Function* CreateFunction(std::function<llvm::Type*(llvm::Module*)>, std::string, Semantic::Function* debug, bool trampoline = false);

            Variable* CreateVariable(std::function<llvm::Type*(llvm::Module*)>, unsigned alignment);
            FunctionCall* CreateFunctionCall(Expression*, std::vector<Expression*>, std::function<llvm::Type*(llvm::Module*)> = std::function<llvm::Type*(llvm::Module*)>());
            StringExpression* CreateStringExpression(std::string);
            NamedGlobalVariable* CreateGlobalVariable(std::string);
            StoreExpression* CreateStore(Expression*, Expression*);
            LoadExpression* CreateLoad(Expression*);
            ReturnStatement* CreateReturn();
            ReturnStatement* CreateReturn(Expression*);
            FunctionValue* CreateFunctionValue(std::string);
            IntegralExpression* CreateIntegralExpression(uint64_t val, bool is_signed, std::function<llvm::Type*(llvm::Module*)> ty);
            ChainExpression* CreateChainExpression(Statement*, Expression*);
            FieldExpression* CreateFieldExpression(Expression*, unsigned);
            FieldExpression* CreateFieldExpression(Expression*, std::function<unsigned()>);
            ParamExpression* CreateParameterExpression(unsigned);
            ParamExpression* CreateParameterExpression(std::function<unsigned()>);
            IfStatement* CreateIfStatement(Expression*, Statement*, Statement*);
            ChainStatement* CreateChainStatement(Statement*, Statement*);
            TruncateExpression* CreateTruncate(Expression*, std::function<llvm::Type*(llvm::Module*)>);
            WhileStatement* CreateWhile(Expression*, Statement*);
            NullExpression* CreateNull(std::function<llvm::Type*(llvm::Module*)> type);
            IntegralLeftShiftExpression* CreateLeftShift(Expression*, Expression*);
            IntegralRightShiftExpression* CreateRightShift(Expression*, Expression*, bool);
            IntegralLessThan* CreateSLT(Expression* lhs, Expression* rhs);
            ZExt* CreateZeroExtension(Expression* val, std::function<llvm::Type*(llvm::Module*)> to);
            NegateExpression* CreateNegateExpression(Expression* val);
            OrExpression* CreateOrExpression(Expression* lhs, Expression* rhs);
            EqualityExpression* CreateEqualityExpression(Expression* lhs, Expression* rhs);
            PlusExpression* CreatePlusExpression(Expression* lhs, Expression* rhs);
            MultiplyExpression* CreateMultiplyExpression(Expression* lhs, Expression* rhs);
            AndExpression* CreateAndExpression(Expression* lhs, Expression* rhs);
            IntegralLessThan* CreateULT(Expression* lhs, Expression* rhs);
            SExt* CreateSignedExtension(Expression* val, std::function<llvm::Type*(llvm::Module*)> to);
            IsNullExpression* CreateIsNullExpression(Expression* val);

            void operator()();
        };
    }
}