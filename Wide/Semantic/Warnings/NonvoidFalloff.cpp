#include <Wide/Semantic/Warnings/NonvoidFalloff.h>
#include <Wide/Semantic/Warnings/GetFunctionName.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Parser/AST.h>

using namespace Wide;
using namespace Semantic;

namespace {
    void GetControlFlowForStatements(Statement* s, std::unordered_map<Statement*, std::unordered_set<Statement*>>& predecessors, Statement* next, Function::WhileStatement* current_while, Statement* while_next) {
        // If I myself have no predecessors, then I can never proceed on to anywhere else, so skip me.
        if (predecessors.find(s) == predecessors.end())
            return;

        if (dynamic_cast<Function::BreakStatement*>(s)) {
            predecessors[while_next].insert(s);
            return;
        }
        if (dynamic_cast<Function::ContinueStatement*>(s)) {
            predecessors[current_while].insert(s);
            return;
        }

        // Nobody can succeed me so just return.
        if (dynamic_cast<Function::ReturnStatement*>(s))
            return;

        // The next statement can be preceeded by me.
        if (dynamic_cast<Expression*>(s)) {
            predecessors[next].insert(s);
            return;
        }
        if (dynamic_cast<Function::VariableStatement*>(s)) {
            predecessors[next].insert(s);
            return;
        }

        if (auto if_stmt = dynamic_cast<Function::IfStatement*>(s)) {
            if (auto constant = dynamic_cast<Boolean*>(if_stmt->cond.get())) {
                // It will be unconditionally executed.                
                if (constant->b) {
                    predecessors[if_stmt->true_br].insert(s);
                    GetControlFlowForStatements(if_stmt->true_br, predecessors, next, current_while, while_next);
                    return;
                }
                // If we have a false branch, it will be unconditionally executed- else, the next statement will be unconditional.
                if (if_stmt->false_br) {
                    predecessors[if_stmt->false_br].insert(s);
                    GetControlFlowForStatements(if_stmt->false_br, predecessors, next, current_while, while_next);
                    return;
                }
                predecessors[next].insert(s);
                return;
            }
            predecessors[if_stmt->true_br].insert(s);
            GetControlFlowForStatements(if_stmt->true_br, predecessors, next, current_while, while_next);
            if (if_stmt->false_br) {
                predecessors[if_stmt->false_br].insert(s);
                GetControlFlowForStatements(if_stmt->false_br, predecessors, next, current_while, while_next);
            } else
                predecessors[next].insert(s);
            return;
        }

        if (auto whil_stmt = dynamic_cast<Function::WhileStatement*>(s)) {
            predecessors[whil_stmt->body.get()].insert(s);
            GetControlFlowForStatements(whil_stmt->body.get(), predecessors, whil_stmt, whil_stmt, next);
            return;
        }

        if (auto comp_stmt = dynamic_cast<Function::CompoundStatement*>(s)) {
            if (comp_stmt->s->active.size() == 0) { predecessors[next].insert(s); return; }
            predecessors[comp_stmt->s->active.front().get()].insert(s);
            for (auto it = comp_stmt->s->active.begin(); it != comp_stmt->s->active.end(); ++it) {
                if (it + 1 == comp_stmt->s->active.end())
                    GetControlFlowForStatements(it->get(), predecessors, next, current_while, while_next);
                else
                    GetControlFlowForStatements(it->get(), predecessors, (it + 1)->get(), current_while, while_next);
            }
            return;
        }

        assert(false && "Found an unknown statement.");
    }
}
std::vector<std::tuple<Lexer::Range, std::string>> Wide::Semantic::GetNonvoidFalloffFunctions(Analyzer& a) {
    std::vector<std::tuple<Lexer::Range, std::string>> out;
    for (auto&& func : a.GetFunctions()) {
        for (auto&& args : func.second) {
            auto instance = args.second.get();
            if (instance->GetSignature()->GetReturnType() == a.GetVoidType()) continue;
            struct EntryExitStatement : Statement {
                void GenerateCode(CodegenContext& con) {}
            };
            EntryExitStatement entry;
            EntryExitStatement exit;
            std::unordered_map<Statement*, std::unordered_set<Statement*>> predecessors;
            if (instance->root_scope->active.size() == 0) {
                out.push_back(std::make_tuple(func.first->where, instance->explain()));
                continue;
            }
            predecessors[instance->root_scope->active.front().get()].insert(&entry);
            for (auto it = instance->root_scope->active.begin(); it != instance->root_scope->active.end(); ++it) {
                if (it + 1 == instance->root_scope->active.end())
                    GetControlFlowForStatements(it->get(), predecessors, &exit, nullptr, nullptr);
                else
                    GetControlFlowForStatements(it->get(), predecessors, (it + 1)->get(), nullptr, nullptr);
            }
            if (predecessors.find(&exit) != predecessors.end())
                out.push_back(std::make_tuple(func.first->where, instance->explain()));
        }
    }
    return out;
}