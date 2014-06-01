#pragma once

#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Expression.h>
#include <vector>
#include <unordered_map>

namespace llvm {
    class Function;
}
namespace Wide {
    namespace AST {
        struct FunctionBase;
        struct Statement;
    }
    namespace Codegen {
        class Function;
    }
    namespace Semantic {
        class FunctionType;
        class UserDefinedType;
        class Function : public MetaType, public Callable {
            llvm::Function* llvmfunc = nullptr;
            std::unique_ptr<Statement> AnalyzeStatement(const AST::Statement*);
            Wide::Util::optional<Type*> ExplicitReturnType;
            Type* ReturnType = nullptr;
            std::vector<Type*> Args;
            const AST::FunctionBase* fun;
            Type* context;
            std::string source_name;
            std::string name;
            std::vector<std::function<std::string(llvm::Module*)>> trampoline;
            std::unordered_set<Type*> ClangContexts;
            enum class State {
                NotYetAnalyzed,
                AnalyzeInProgress,
                AnalyzeCompleted
            };
            State s;
            void ComputeReturnType(); 
            std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final { 
                return BuildCall(BuildValueConstruction(Expressions(), c), std::move(args), c);
            }
            std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
                return AdjustArgumentsForTypes(std::move(args), Args, c);
            }
        public:
            // Public for analysis.
            struct Scope;
            struct LocalVariable : public Expression {
                std::unique_ptr<Expression> construction;
                Wide::Util::optional<unsigned> tuple_num;
                Type* tup_ty = nullptr;
                Type* var_type = nullptr;
                std::unique_ptr<ImplicitTemporaryExpr> variable;
                Expression* init_expr;
                Function* self;
                Lexer::Range where;

                void OnNodeChanged(Node* n, Change what) override final;
                LocalVariable(Expression* ex, unsigned u, Function* self, Lexer::Range where);
                LocalVariable(Expression* ex, Function* self, Lexer::Range where);
                void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
                llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
                Type* GetType() override final;
            };

            struct ReturnStatement : public Statement {
                Lexer::Range where;
                Function* self;
                std::function<void(llvm::Module* module, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> destructors;
                std::unique_ptr<Expression> ret_expr;
                std::unique_ptr<Expression> build;

                ReturnStatement(Function* f, std::unique_ptr<Expression> expr, Scope* current, Lexer::Range where);
                void OnNodeChanged(Node* n, Change what) override final;
                void DestroyLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
                void GenerateCode(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
            };

            struct CompoundStatement : public Statement {
                CompoundStatement(Scope* s);
                Scope* s;
                void DestroyLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas);
                void GenerateCode(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas);
            };

            struct WhileStatement : Statement {
                Function* self;
                Lexer::Range where;
                Expression* cond;
                Statement* body;
                std::unique_ptr<Expression> boolconvert;
                llvm::BasicBlock* continue_bb = nullptr;
                llvm::BasicBlock* check_bb = nullptr;

                WhileStatement(Expression* ex, Lexer::Range where, Function* s);
                void OnNodeChanged(Node* n, Change what) override final;
                void DestroyLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
                void GenerateCode(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;                
            };

            struct VariableStatement : public Statement {
                VariableStatement(std::vector<LocalVariable*> locs, std::unique_ptr<Expression> expr);
                std::unique_ptr<Expression> init_expr;
                std::vector<LocalVariable*> locals;

                void DestroyLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
                void GenerateCode(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
            };

            struct Scope {
                Scope(Scope* s);
                Scope* parent;
                std::vector<std::unique_ptr<Scope>> children;
                std::unordered_map<std::string, std::pair<std::unique_ptr<Expression>, Lexer::Range>> named_variables;
                std::vector<std::unique_ptr<Statement>> active;
                WhileStatement* current_while;
                std::unique_ptr<Expression> LookupLocal(std::string name);
                std::function<void(llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> DestroyLocals();
                std::function<void(llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> DestroyAllLocals();
                std::function<void(llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> DestroyWhileBody();
                std::function<void(llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> DestroyWhileBodyAndCond();
                WhileStatement* GetCurrentWhile();
            };
            struct LocalScope;
            struct IfStatement : Statement {
                Function* self;
                Lexer::Range where;
                Expression* cond;
                Statement* true_br;
                Statement* false_br;
                std::unique_ptr<Expression> boolconvert;

                IfStatement(Expression* cond, Statement* true_b, Statement* false_b, Lexer::Range where, Function* s);
                void OnNodeChanged(Node* n, Change what) override final;
                void DestroyLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
                void GenerateCode(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;

            };
            struct BreakStatement : public Statement {
                WhileStatement* while_stmt;
                std::function<void(llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> destroy_locals;

                BreakStatement(Scope* s);
                void DestroyLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
                void GenerateCode(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
            };
            struct ContinueStatement : public Statement  {
                ContinueStatement(Scope* s);
                WhileStatement* while_stmt;
                std::function<void(llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> destroy_locals;

                void DestroyLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas);
                void GenerateCode(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas);
            };
            struct Parameter : Expression {
                Lexer::Range where;
                Function* self;
                unsigned num;
                std::unique_ptr<Expression> destructor;
                Type* cur_ty = nullptr;                

                Parameter(Function* s, unsigned n, Lexer::Range where);
                void OnNodeChanged(Node* n, Change what) override final;
                Type* GetType() override final;
                void DestroyExpressionLocals(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
                llvm::Value* ComputeValue(llvm::Module* module, llvm::IRBuilder<>& bb, llvm::IRBuilder<>& allocas) override final;
            };
        private:
            std::unordered_set<ReturnStatement*> returns;
        public:
            std::unique_ptr<Scope> root_scope;
        private:
            Scope* current_scope;
        public:

            void ComputeBody();
            void EmitCode(llvm::Module* module);
            Function(std::vector<Type*> args, const AST::FunctionBase* astfun, Analyzer& a, Type* container, std::string name);        

            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& where) override final;
     
            std::unique_ptr<Expression> BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) override final;
            std::string GetName();
            Type* GetContext() override final { return context; }

            FunctionType* GetSignature();
            std::unique_ptr<Expression> LookupLocal(std::string name);
            Type* GetConstantContext() override final;
            void AddExportName(std::string name) {trampoline.push_back([name](llvm::Module*) { return name; }); }
            std::string explain() override final;
            std::string GetSourceName() { return source_name; }
            ~Function();

            const std::unordered_set<Type*>& GetClangContexts() { return ClangContexts; }
        };
    }
}