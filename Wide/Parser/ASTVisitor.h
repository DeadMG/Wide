#pragma once

#include <Wide/Parser/AST.h>

namespace Wide {
    namespace AST {
        template<typename F> class Visitor {
            F& crtp_cast() { return *static_cast<F*>(this); }
        public:
            void VisitStatement(Statement* s) {
                if (!s) return;
                if (auto whil = dynamic_cast<WhileStatement*>(s)) { crtp_cast().VisitWhileStatement(whil); return; }
                if (auto br = dynamic_cast<IfStatement*>(s)) { crtp_cast().VisitIfStatement(br); return; }
                if (auto ret = dynamic_cast<Return*>(s)) { crtp_cast().VisitReturnStatement(ret); return; }
                if (auto comp = dynamic_cast<CompoundStatement*>(s)) { crtp_cast().VisitCompoundStatement(comp); return; }
                if (auto var = dynamic_cast<VariableStatement*>(s)) { crtp_cast().VisitVariableStatement(var); return; }
                if (auto expr = dynamic_cast<Expression*>(s)) { crtp_cast().VisitExpression(expr); return; }
                assert(false && "Internal Compiler Error: Encountered an unknown statement in AST::Visitor.");
            }

            void VisitExpression(Expression* e) {
                if (auto ident = dynamic_cast<IdentifierExpr*>(e)) { crtp_cast().VisitIdentifier(ident); return; }
                if (auto str = dynamic_cast<StringExpr*>(e)) { crtp_cast().VisitString(str); return; }
                if (auto mem = dynamic_cast<MemAccessExpr*>(e)) { crtp_cast().VisitMemberAccess(mem); return; }
                if (auto in = dynamic_cast<IntegerExpression*>(e)) { crtp_cast().VisitInteger(in); return; }
                if (auto call = dynamic_cast<FunctionCallExpr*>(e)) { crtp_cast().VisitCall(call); return; }
                if (auto call = dynamic_cast<MetaCallExpr*>(e)) { crtp_cast().VisitMetaCall(call); return; }
                if (auto lam = dynamic_cast<Lambda*>(e)) { crtp_cast().VisitLambda(lam); return; }
                if (auto deref = dynamic_cast<DereferenceExpression*>(e)) { crtp_cast().VisitDereference(deref); return; }
                if (auto neg = dynamic_cast<NegateExpression*>(e)) { crtp_cast().VisitNegate(neg); return; }
                if (auto inc = dynamic_cast<Increment*>(e)) { crtp_cast().VisitIncrement(inc); return; }
                if (auto ptr = dynamic_cast<PointerAccess*>(e)) { crtp_cast().VisitPointerAccess(ptr); return; }
                if (auto self = dynamic_cast<ThisExpression*>(e)) { crtp_cast().VisitThisExpression(self); return; }
                if (auto dec = dynamic_cast<Decrement*>(e)) { return crtp_cast().VisitDecrement(dec); }
				if (auto bin = dynamic_cast<BinaryExpression*>(e)) { return crtp_cast().VisitBinaryExpression(bin); }
                assert(false && "Internal Compiler Error: Encountered unknown expression node in AST::Visitor.");
            }            

            void VisitWhileStatement(WhileStatement* s) { 
                crtp_cast().VisitExpression(s->condition);
                crtp_cast().VisitStatement(s->body);
            }
            void VisitReturnStatement(Return* r) { if (r->RetExpr) crtp_cast().VisitExpression(r->RetExpr); }
            void VisitCompoundStatement(CompoundStatement* s) { 
                for(auto&& x : s->stmts)
                    crtp_cast().VisitStatement(x);
            }
            void VisitVariableStatement(VariableStatement* s) { crtp_cast().VisitExpression(s->initializer); }
            void VisitIfStatement(IfStatement* s) { 
                crtp_cast().VisitExpression(s->condition);
                crtp_cast().VisitStatement(s->true_statement);
                crtp_cast().VisitStatement(s->false_statement); 
            }

            void VisitBinaryExpression(BinaryExpression* expr) {
                crtp_cast().VisitExpression(expr->lhs);
                crtp_cast().VisitExpression(expr->rhs);
            }

            void VisitLambda(Lambda* l) {
                for(auto&& x : l->statements)
                    crtp_cast().VisitStatement(x);
                for(auto&& x : l->Captures)
                    crtp_cast().VisitLambdaCapture(&x);
                for(auto&& x : l->args)
                    crtp_cast().VisitLambdaArgument(&x);
            }
            void VisitCall(FunctionCallExpr* e) {
                crtp_cast().VisitExpression(e->callee);
                for(auto&& x : e->args)
                    crtp_cast().VisitExpression(x);
            }
            void VisitMetaCall(MetaCallExpr* e) {
                crtp_cast().VisitExpression(e->callee);
                for(auto&& x : e->args)
                    crtp_cast().VisitExpression(x);
            }
            void VisitLambdaCapture(VariableStatement* l) {
                crtp_cast().VisitVariableStatement(l);
            }
            void VisitLambdaArgument(FunctionArgument* arg) {
                crtp_cast().VisitExpression(arg->type);
            }
            void VisitIdentifier(IdentifierExpr* e) {}
            void VisitString(StringExpr* e) {}
            void VisitDereference(DereferenceExpression* e) { return crtp_cast().VisitExpression(e->ex); }
            void VisitMemberAccess(MemAccessExpr* e) { return crtp_cast().VisitExpression(e->expr); }  
            void VisitInteger(IntegerExpression* e) { }
            void VisitNegate(NegateExpression* e) { return crtp_cast().VisitExpression(e->ex); }
            void VisitIncrement(Increment* i) { return crtp_cast().VisitExpression(i->ex); }
            void VisitPointerAccess(PointerAccess* p) { return crtp_cast().VisitExpression(p->ex); }
            void VisitThisExpression(ThisExpression* expr) {}
            void VisitDecrement(Decrement* d) { return crtp_cast().VisitExpression(d->ex); }
        };
    }
}