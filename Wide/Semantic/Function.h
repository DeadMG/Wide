#pragma once

#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Expression.h>
#include <vector>
#include <unordered_map>
#include <memory>

namespace llvm {
    class Function;
}
namespace Wide {
    namespace Parse {
        struct FunctionBase;
        struct Statement;
        struct TryCatch;
    }
    namespace Semantic {
        class FunctionType;
        class UserDefinedType;
        class Function : public MetaType, public Callable {
            llvm::Function* llvmfunc = nullptr;
            std::unique_ptr<Statement> AnalyzeStatement(const Parse::Statement*);
            Wide::Util::optional<Type*> ExplicitReturnType;
            Type* ReturnType = nullptr;
            std::vector<Type*> Args;
            const Parse::FunctionBase* fun;
            Type* context;
            std::string source_name;
            std::string name;
            std::vector<std::function<std::string(llvm::Module*)>> trampoline;

            // You can only be exported as constructors of one, or nonstatic member of one, class.
            Wide::Util::optional<Semantic::ConstructorContext*> ConstructorContext;
            Wide::Util::optional<Type*> NonstaticMemberContext;
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
                Lexer::Range init_where;

                void OnNodeChanged(Node* n, Change what) override final;
                LocalVariable(Expression* ex, unsigned u, Function* self, Lexer::Range where, Lexer::Range init_where);
                LocalVariable(Expression* ex, Function* self, Lexer::Range where, Lexer::Range init_where);
                llvm::Value* ComputeValue(CodegenContext& con) override final;
                Type* GetType() override final;
            };

            struct ReturnStatement : public Statement {
                Lexer::Range where;
                Function* self;
                std::unique_ptr<Expression> ret_expr;
                std::unique_ptr<Expression> build;

                ReturnStatement(Function* f, std::unique_ptr<Expression> expr, Scope* current, Lexer::Range where);
                void OnNodeChanged(Node* n, Change what) override final;
                void GenerateCode(CodegenContext& con) override final;
            };

            struct CompoundStatement : public Statement {
                CompoundStatement(Scope* s);
                Scope* s;
                void GenerateCode(CodegenContext& con);
            };

            struct WhileStatement : Statement {
                Function* self;
                Lexer::Range where;
                Expression* cond;
                Statement* body;
                std::unique_ptr<Expression> boolconvert;
                llvm::BasicBlock* continue_bb = nullptr;
                llvm::BasicBlock* check_bb = nullptr;
                CodegenContext* source_con = nullptr;
                CodegenContext* condition_con = nullptr;

                WhileStatement(Expression* ex, Lexer::Range where, Function* s);
                void OnNodeChanged(Node* n, Change what) override final;
                void GenerateCode(CodegenContext& con) override final;
            };

            struct VariableStatement : public Statement {
                VariableStatement(std::vector<LocalVariable*> locs, std::unique_ptr<Expression> expr);
                std::unique_ptr<Expression> init_expr;
                std::vector<LocalVariable*> locals;

                void GenerateCode(CodegenContext& con) override final;
            };

            struct Scope {
                Scope(Scope* s);
                Scope* parent;
                std::vector<std::unique_ptr<Scope>> children;
                std::unordered_map<std::string, std::pair<std::unique_ptr<Expression>, Lexer::Range>> named_variables;
                std::vector<std::unique_ptr<Statement>> active;
                WhileStatement* current_while;
                std::unique_ptr<Expression> LookupLocal(std::string name);
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
                void GenerateCode(CodegenContext& con) override final;

            };
            struct BreakStatement : public Statement {
                WhileStatement* while_stmt;
                std::function<void(llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> destroy_locals;

                BreakStatement(Scope* s);
                void GenerateCode(CodegenContext& con) override final;
            };
            struct ContinueStatement : public Statement  {
                ContinueStatement(Scope* s);
                WhileStatement* while_stmt;
                std::function<void(llvm::Module*, llvm::IRBuilder<>&, llvm::IRBuilder<>&)> destroy_locals;

                void GenerateCode(CodegenContext& con);
            };

            struct ThrowStatement : public Statement {
                Type* ty;
                std::unique_ptr<Expression> exception;
                std::unique_ptr<Expression> except_memory;
                ThrowStatement(std::unique_ptr<Expression> expr, Context c);
                void GenerateCode(CodegenContext& con);
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
                void DestroyExpressionLocals(CodegenContext& con) override final;
                llvm::Value* ComputeValue(CodegenContext& con) override final;
            };
            struct TryStatement : public Statement {
                struct CatchParameter : Expression {
                    llvm::Value* param;
                    Type* t;
                    llvm::Value* ComputeValue(CodegenContext& con) override final { return con->CreatePointerCast(param, t->GetLLVMType(con)); }
                    Type* GetType() override final { return t; }
                };
                struct Catch {
                    Catch(Type* t, std::vector<std::unique_ptr<Statement>> stmts, std::unique_ptr<CatchParameter> catch_param)
                    : t(t), stmts(std::move(stmts)), catch_param(std::move(catch_param)) {}
                    Catch(Catch&& other)
                    : t(other.t)
                    , stmts(std::move(other.stmts))
                    , catch_param(std::move(other.catch_param)) {}
                    Catch& operator=(Catch&& other) {
                        t = other.t;
                        stmts = std::move(other.stmts);
                        catch_param = std::move(other.catch_param);
                    }
                    Type* t; // Null for catch-all
                    std::vector<std::unique_ptr<Statement>> stmts;
                    std::unique_ptr<CatchParameter> catch_param;
                };
                TryStatement(std::vector<std::unique_ptr<Statement>> stmts, std::vector<Catch> catches, Analyzer& a)
                    : statements(std::move(stmts)), catches(std::move(catches)), a(a) {}
                Analyzer& a;
                std::vector<Catch> catches;
                std::vector<std::unique_ptr<Statement>> statements;
                void GenerateCode(CodegenContext& con);
            };
            struct RethrowStatement : public Statement {
                void GenerateCode(CodegenContext& con);
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
            Function(std::vector<Type*> args, const Parse::FunctionBase* astfun, Analyzer& a, Type* container, std::string name);

            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& where) override final;
     
            std::unique_ptr<Expression> BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) override final;
            std::string GetName();
            Type* GetContext() override final { return context; }
            Type* GetNonstaticMemberContext() { if (NonstaticMemberContext) return *NonstaticMemberContext; return nullptr; }

            FunctionType* GetSignature();
            std::unique_ptr<Expression> LookupLocal(std::string name);
            Type* GetConstantContext() override final;
            void AddExportName(std::string name) {trampoline.push_back([name](llvm::Module*) { return name; }); }
            void AddExportName(std::function<std::string(llvm::Module*)> mod) { trampoline.push_back(mod); }
            std::string explain() override final;
            std::string GetSourceName() { return source_name; }
            ~Function();

            const std::unordered_set<Type*>& GetClangContexts() { return ClangContexts; }
        };
    }
}