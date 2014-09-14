#pragma once 

#include <Wide/Lexer/Token.h>
#include <Wide/Util/Ranges/Optional.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Hashers.h>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include <functional>
#include <string>
#include <cassert>
#include <memory>

#pragma warning(push, 0)
#include <llvm/IR/IRBuilder.h>
#include <llvm/ADT/APInt.h>
#pragma warning(pop)

namespace Wide {
    namespace Semantic {
        struct Context {
            Context(Type* f, Lexer::Range r) : from(f), where(r) {}
            Type* from;
            Lexer::Range where;
        };
        struct Type;
        struct Context;
        struct CodegenContext;
        enum Change {
            Contents,
            Destroyed
        };
        struct Node {
        private:
            std::unordered_set<Node*> listeners;
            std::unordered_set<Node*> listening_to;
            void AddChangedListener(Node* n) { listeners.insert(n); }
            void RemoveChangedListener(Node* n) { listeners.erase(n); }
        protected:
            virtual void OnNodeChanged(Node* n, Change what) {}
            void ListenToNode(Node* n) {
                n->AddChangedListener(this);
                listening_to.insert(n);
            }
            void StopListeningToNode(Node* n) {
                n->RemoveChangedListener(this);
                listening_to.erase(n);
            }
            void OnChange() {
                for (auto node : listeners)
                    node->OnNodeChanged(this, Change::Contents);
            }
        public:
            virtual ~Node() {
                for (auto node : listening_to)
                    node->listeners.erase(this);
                for (auto node : listeners) {
                    // ENABLE TO DEBUG ACCIDENTALLY DESTROYED EXPRESSIONS
                    //    node->OnNodeChanged(this, Change::Destroyed);
                    node->listening_to.erase(this);
                }
            }
        };
        struct Statement : public Node {
            virtual void GenerateCode(CodegenContext& con) = 0;
        };
        struct ConstantExpression;
        struct Expression : public Statement {
            virtual Type* GetType() = 0; // If the type is unknown then nullptr
            llvm::Value* GetValue(CodegenContext& con);
            virtual ConstantExpression* IsConstantExpression() { return nullptr; } // If not constant then nullptr
        private:
            Wide::Util::optional<llvm::Value*> val;
            void GenerateCode(CodegenContext& con) override final {
                GetValue(con);
            }
            virtual llvm::Value* ComputeValue(CodegenContext& con) = 0;
        };

        struct ImplicitLoadExpr : public Expression {
            ImplicitLoadExpr(std::shared_ptr<Expression> expr);
            std::shared_ptr<Expression> src;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct ImplicitStoreExpr : public Expression {
            ImplicitStoreExpr(std::shared_ptr<Expression> memory, std::shared_ptr<Expression> value);
            std::shared_ptr<Expression> mem, val;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct ImplicitTemporaryExpr : public Expression {
            ImplicitTemporaryExpr(Type* what, Context c);
            Type* of;
            llvm::Value* alloc;
            Context c;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct LvalueCast : public Expression {
            LvalueCast(std::shared_ptr<Expression> expr);
            std::shared_ptr<Expression> expr;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct RvalueCast : public Expression {
            RvalueCast(std::shared_ptr<Expression> expr);
            std::shared_ptr<Expression> expr;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct ImplicitAddressOf : public Expression {
            ImplicitAddressOf(std::shared_ptr<Expression>, Context c);
            Context c;
            std::shared_ptr<Expression> expr;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };
        
        struct Chain : Expression {
            Chain(std::shared_ptr<Expression> effect, std::shared_ptr<Expression> result);
            std::shared_ptr<Expression> SideEffect;
            std::shared_ptr<Expression> result;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
            ConstantExpression* IsConstantExpression() override final;
        };

        struct DestructorCall : Expression {
            DestructorCall(std::function<void(CodegenContext&)> destructor, Analyzer& a);
            std::function<void(CodegenContext&)> destructor;
            Analyzer* a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct ConstantExpression : Expression {
            ConstantExpression* IsConstantExpression() override final { return this; }
        };

        struct String : ConstantExpression {
            String(std::string s, Analyzer& an);
            std::string str;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct Integer : ConstantExpression {
            Integer(llvm::APInt val, Analyzer& an);
            llvm::APInt value;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        struct Boolean : ConstantExpression {
            Boolean(bool b, Analyzer& a);
            bool b;
            Analyzer& a;
            Type* GetType() override final;
            llvm::Value* ComputeValue(CodegenContext& con) override final;
        };

        std::shared_ptr<Expression> CreatePrimUnOp(std::shared_ptr<Expression> self, Type* ret, std::function<llvm::Value*(llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimAssOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimOp(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, Type* ret, std::function<llvm::Value*(llvm::Value*, llvm::Value*, CodegenContext&)>);
        std::shared_ptr<Expression> CreatePrimGlobal(Type* ret, std::function<llvm::Value*(CodegenContext&)>);
        std::shared_ptr<Expression> BuildValue(std::shared_ptr<Expression>);
        std::shared_ptr<Expression> BuildChain(std::shared_ptr<Expression>, std::shared_ptr<Expression>);
    }
}