#pragma once

#include <Wide/Parser/AST.h>

namespace Wide {
    namespace AST {
        template<typename F> class Visitor {
            F& crtp_cast() { return *static_cast<F*>(this); }
        public:
            void VisitStatement(const Statement* s) {
                if (!s) return;
                if (auto whil = dynamic_cast<const While*>(s)) { return crtp_cast().VisitWhile(whil); }
                if (auto br = dynamic_cast<const If*>(s)) { return crtp_cast().VisitIf(br); }
                if (auto ret = dynamic_cast<const Return*>(s)) { return crtp_cast().VisitReturn(ret); }
                if (auto comp = dynamic_cast<const CompoundStatement*>(s)) { return crtp_cast().VisitCompoundStatement(comp); }
                if (auto var = dynamic_cast<const Variable*>(s)) { return crtp_cast().VisitVariable(var); }
                if (auto expr = dynamic_cast<const Expression*>(s)) { return crtp_cast().VisitExpression(expr); }
                assert(false && "Internal Compiler Error: Encountered an unknown statement in AST::Visitor.");
            }

            void VisitExpression(const Expression* e) {
                if (auto ident = dynamic_cast<const Identifier*>(e)) { return crtp_cast().VisitIdentifier(ident); }
                if (auto str = dynamic_cast<const String*>(e)) { return crtp_cast().VisitString(str); }
                if (auto mem = dynamic_cast<const MemberAccess*>(e)) { return crtp_cast().VisitMemberAccess(mem); }
                if (auto in = dynamic_cast<const Integer*>(e)) { return crtp_cast().VisitInteger(in); }
                if (auto call = dynamic_cast<const FunctionCall*>(e)) { return crtp_cast().VisitCall(call); }
                if (auto call = dynamic_cast<const MetaCall*>(e)) { return crtp_cast().VisitMetaCall(call); }
                if (auto lam = dynamic_cast<const Lambda*>(e)) { return crtp_cast().VisitLambda(lam); }
                if (auto deref = dynamic_cast<const Dereference*>(e)) { return crtp_cast().VisitDereference(deref); }
                if (auto neg = dynamic_cast<const Negate*>(e)) { return crtp_cast().VisitNegate(neg); }
                if (auto inc = dynamic_cast<const Increment*>(e)) { return crtp_cast().VisitIncrement(inc); }
                if (auto ptr = dynamic_cast<const PointerMemberAccess*>(e)) { return crtp_cast().VisitPointerAccess(ptr); }
                if (auto self = dynamic_cast<const This*>(e)) { return crtp_cast().VisitThis(self); }
                if (auto dec = dynamic_cast<const Decrement*>(e)) { return crtp_cast().VisitDecrement(dec); }
                if (auto bin = dynamic_cast<const BinaryExpression*>(e)) { return crtp_cast().VisitBinaryExpression(bin); }
                assert(false && "Internal Compiler Error: Encountered unknown expression node in AST::Visitor.");
            }            

            void VisitWhile(const While* s) { 
                crtp_cast().VisitExpression(s->condition);
                crtp_cast().VisitStatement(s->body);
            }
            void VisitReturn(const Return* r) { if (r->RetExpr) crtp_cast().VisitExpression(r->RetExpr); }
            void VisitCompoundStatement(const CompoundStatement* s) { 
                for(auto&& x : s->stmts)
                    crtp_cast().VisitStatement(x);
            }
            void VisitVariable(const Variable* s) { crtp_cast().VisitExpression(s->initializer); }
            void VisitIf(const If* s) { 
                crtp_cast().VisitExpression(s->condition);
                crtp_cast().VisitStatement(s->true_statement);
                crtp_cast().VisitStatement(s->false_statement); 
            }

            void VisitBinaryExpression(const BinaryExpression* expr) {
                crtp_cast().VisitExpression(expr->lhs);
                crtp_cast().VisitExpression(expr->rhs);
            }

            void VisitLambda(const Lambda* l) {
                for(auto&& x : l->statements)
                    crtp_cast().VisitStatement(x);
                for(auto&& x : l->Captures)
                    crtp_cast().VisitLambdaCapture(&x);
                for(auto&& x : l->args)
                    crtp_cast().VisitLambdaArgument(&x);
            }
            void VisitCall(const FunctionCall* e) {
                crtp_cast().VisitExpression(e->callee);
                for(auto&& x : e->args)
                    crtp_cast().VisitExpression(x);
            }
            void VisitMetaCall(const MetaCall* e) {
                crtp_cast().VisitExpression(e->callee);
                for(auto&& x : e->args)
                    crtp_cast().VisitExpression(x);
            }
            void VisitLambdaCapture(const Variable* l) {
                crtp_cast().VisitVariableStatement(l);
            }
            void VisitLambdaArgument(const FunctionArgument* arg) {
                crtp_cast().VisitExpression(arg->type);
            }
            void VisitIdentifier(const Identifier* e) {}
            void VisitString(const String* e) {}
            void VisitDereference(const Dereference* e) { return crtp_cast().VisitExpression(e->ex); }
            void VisitMemberAccess(const MemberAccess* e) { return crtp_cast().VisitExpression(e->expr); }  
            void VisitInteger(const Integer* e) { }
            void VisitNegate(const Negate* e) { return crtp_cast().VisitExpression(e->ex); }
            void VisitIncrement(const Increment* i) { return crtp_cast().VisitExpression(i->ex); }
            void VisitPointerAccess(const PointerMemberAccess* p) { return crtp_cast().VisitExpression(p->ex); }
            void VisitThis(const This* expr) {}
            void VisitDecrement(const Decrement* d) { return crtp_cast().VisitExpression(d->ex); }
        };
    }
}