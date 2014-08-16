#pragma once

#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Expression.h>
#include <vector>
#include <unordered_map>
#include <memory>

namespace llvm {
    class Function;
}
namespace clang {
    class FunctionDecl;
}
namespace Wide {
    namespace Parse {
        struct FunctionBase;
        struct Statement;
        struct TryCatch;
    }
    namespace Semantic {
        class WideFunctionType;
        class UserDefinedType;
        class ClangFunctionType;
        class Function : public MetaType, public Callable {
            llvm::Function* llvmfunc = nullptr;
            std::shared_ptr<Statement> AnalyzeStatement(const Parse::Statement*);
            Wide::Util::optional<Type*> ExplicitReturnType;
            Type* ReturnType = nullptr;
            std::vector<Type*> Args;
            const Parse::FunctionBase* fun;
            Type* context;
            std::string source_name;
            std::vector<std::function<void(llvm::Module*)>> trampoline;
            std::vector<std::shared_ptr<Expression>> parameters;
            std::vector<std::tuple<std::function<llvm::Function*(llvm::Module*)>, ClangFunctionType*, clang::FunctionDecl*>> clang_exports;

            // You can only be exported as constructors of one, or nonstatic member of one, class.
            Wide::Util::optional<Semantic::ConstructorContext*> ConstructorContext;
            Wide::Util::optional<Type*> NonstaticMemberContext;
            enum class State {
                NotYetAnalyzed,
                AnalyzeInProgress,
                AnalyzeCompleted
            };
            State s;
            void ComputeReturnType(); 
            std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) override final { 
                return Type::BuildCall(BuildValueConstruction({}, c), std::move(args), c);
            }
            std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) override final;
        public:
            // Public for analysis.
            struct Scope;
            struct LocalVariable : public Expression {
                std::shared_ptr<Expression> construction;
                Wide::Util::optional<unsigned> tuple_num;
                Type* tup_ty = nullptr;
                Type* var_type = nullptr;
                std::shared_ptr<ImplicitTemporaryExpr> variable;
                std::shared_ptr<Expression> init_expr;
                std::function<void(CodegenContext&)> destructor;
                Function* self;
                Lexer::Range where;
                Lexer::Range init_where;

                void OnNodeChanged(Node* n, Change what) override final;
                LocalVariable(std::shared_ptr<Expression> ex, unsigned u, Function* self, Lexer::Range where, Lexer::Range init_where);
                LocalVariable(std::shared_ptr<Expression> ex, Function* self, Lexer::Range where, Lexer::Range init_where);
                llvm::Value* ComputeValue(CodegenContext& con) override final;
                Type* GetType() override final;
            };

            struct ReturnStatement : public Statement {
                Lexer::Range where;
                Function* self;
                std::shared_ptr<Expression> ret_expr;
                std::shared_ptr<Expression> build;

                ReturnStatement(Function* f, std::shared_ptr<Expression> expr, Scope* current, Lexer::Range where);
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
                std::shared_ptr<Expression> cond;
                std::shared_ptr<Statement> body;
                std::shared_ptr<Expression> boolconvert;
                llvm::BasicBlock* continue_bb = nullptr;
                llvm::BasicBlock* check_bb = nullptr;
                CodegenContext* source_con = nullptr;
                CodegenContext* condition_con = nullptr;

                WhileStatement(std::shared_ptr<Expression> ex, Lexer::Range where, Function* s);
                void OnNodeChanged(Node* n, Change what) override final;
                void GenerateCode(CodegenContext& con) override final;
            };

            struct VariableStatement : public Statement {
                VariableStatement(std::vector<LocalVariable*> locs, std::shared_ptr<Expression> expr);
                std::shared_ptr<Expression> init_expr;
                std::vector<LocalVariable*> locals;

                void GenerateCode(CodegenContext& con) override final;
            };

            struct Scope {
                Scope(Scope* s);
                Scope* parent;
                std::vector<std::unique_ptr<Scope>> children;
                std::unordered_map<std::string, std::pair<std::shared_ptr<Expression>, Lexer::Range>> named_variables;
                std::vector<std::shared_ptr<Statement>> active;
                WhileStatement* current_while;
                std::shared_ptr<Expression> LookupLocal(std::string name);
                WhileStatement* GetCurrentWhile();
            };
            struct LocalScope;
            struct IfStatement : Statement {
                Function* self;
                Lexer::Range where;
                std::shared_ptr<Expression> cond;
                Statement* true_br;
                Statement* false_br;
                std::shared_ptr<Expression> boolconvert;

                IfStatement(std::shared_ptr<Expression> cond, Statement* true_b, Statement* false_b, Lexer::Range where, Function* s);
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
                struct ExceptionAllocateMemory;
                Type* ty;
                std::shared_ptr<Expression> exception;
                std::function<llvm::Constant*(llvm::Module*)> RTTI;
                std::shared_ptr<ExceptionAllocateMemory> except_memory;
                ThrowStatement(std::shared_ptr<Expression> expr, Context c);
                void GenerateCode(CodegenContext& con);
            };
                        
            struct Parameter : Expression, std::enable_shared_from_this<Parameter> {
                Lexer::Range where;
                Function* self;
                unsigned num;
                std::function<void(CodegenContext&)> destructor;
                Type* cur_ty = nullptr;                

                Parameter(Function* s, unsigned n, Lexer::Range where);
                void OnNodeChanged(Node* n, Change what) override final;
                Type* GetType() override final;
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
                    Catch(Type* t, std::vector<std::shared_ptr<Statement>> stmts, std::shared_ptr<CatchParameter> catch_param)
                        : t(t), stmts(std::move(stmts)), catch_param(std::move(catch_param)) {
                        if (t)
                            RTTI = t->GetRTTI();
                    }
                    Catch(Catch&& other)
                    : t(other.t)
                    , stmts(std::move(other.stmts))
                    , catch_param(std::move(other.catch_param))
                    , RTTI(std::move(other.RTTI)) {}
                    Catch& operator=(Catch&& other) {
                        t = other.t;
                        stmts = std::move(other.stmts);
                        catch_param = std::move(other.catch_param);
                        RTTI = std::move(other.RTTI);
                    }
                    Type* t; // Null for catch-all
                    std::function<llvm::Constant*(llvm::Module*)> RTTI;
                    std::vector<std::shared_ptr<Statement>> stmts;
                    std::shared_ptr<CatchParameter> catch_param;
                };
                TryStatement(std::vector<std::shared_ptr<Statement>> stmts, std::vector<Catch> catches, Analyzer& a)
                    : statements(std::move(stmts)), catches(std::move(catches)), a(a) {}
                Analyzer& a;
                std::vector<Catch> catches;
                std::vector<std::shared_ptr<Statement>> statements;
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
            llvm::Function* EmitCode(llvm::Module* module);
            Function(std::vector<Type*> args, const Parse::FunctionBase* astfun, Analyzer& a, Type* container, std::string name, Type* nonstatic_context);

            Wide::Util::optional<clang::QualType> GetClangType(ClangTU& where) override final;
     
            std::shared_ptr<Expression> ConstructCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            Type* GetContext() override final { return context; }
            Type* GetNonstaticMemberContext() { if (NonstaticMemberContext) return *NonstaticMemberContext; return nullptr; }

            WideFunctionType* GetSignature();
            std::shared_ptr<Expression> LookupLocal(Parse::Name name);
            Type* GetConstantContext() override final;
            std::string explain() override final;
            std::string GetSourceName() { return source_name; }
            ~Function();
            std::shared_ptr<Expression> GetStaticSelf();
            void AddExportName(std::function<void(llvm::Module*)> mod);
        };
    }
}