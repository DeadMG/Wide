#include <Wide/Semantic/Functions/FunctionSkeleton.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/StringType.h>
#include <Wide/Semantic/LambdaType.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/Functions/Function.h>
#include <unordered_set>
#include <sstream>

#pragma warning(push, 0)
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/raw_os_ostream.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <CodeGen/CodeGenTypes.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;
using namespace Functions;

Scope::Scope(Scope* s) : parent(s), control_flow(nullptr) {
    if (parent)
        parent->children.push_back(std::unique_ptr<Scope>(this));
}

ControlFlowStatement* Scope::GetCurrentControlFlow() {
    if (control_flow)
        return control_flow;
    if (parent)
        return parent->GetCurrentControlFlow();
    return nullptr;
}

std::shared_ptr<Expression> Scope::LookupLocal(std::string name) {
    if (named_variables.find(name) != named_variables.end())
        return named_variables.at(name).first;
    if (parent)
        return parent->LookupLocal(name);
    return nullptr;
}
FunctionSkeleton::FunctionSkeleton(const Parse::FunctionBase* astfun, Analyzer& a, Location where)
    : fun(astfun)
    , root_scope(nullptr)
    , analyzer(a)
    , context(where)
{
    bool exported_this = false;
    // Deal with the exports first, if any
    if (auto fun = dynamic_cast<const Parse::AttributeFunctionBase*>(this->fun)) {
        for (auto&& attr : fun->attributes) {
            if (auto name = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
                if (auto string = boost::get<std::string>(&name->val.name)) {
                    if (*string == "export") {
                        auto expr = a.AnalyzeExpression(context, attr.initializer.get(), nullptr);
                        auto overset = dynamic_cast<OverloadSet*>(expr->GetType()->Decay());
                        if (!overset) {
                            ExportErrors[attr.initializer.get()] = Wide::Memory::MakeUnique<Semantic::SpecificError<ExportNonOverloadSet>>(analyzer, attr.initializer->location, "Export initializer not an overload set.");
                            continue;
                        }
                        auto tuanddecl = overset->GetSingleFunction();
                        if (!tuanddecl.second) {
                            ExportErrors[attr.initializer.get()] = Wide::Memory::MakeUnique<Semantic::SpecificError<ExportNotSingleFunction>>(analyzer, attr.initializer->location, "Export initializer not a function.");
                            continue;
                        }
                        auto tu = tuanddecl.first;
                        auto decl = tuanddecl.second;
                        std::function<llvm::Function*(llvm::Module*)> source;

                        if (auto des = llvm::dyn_cast<clang::CXXDestructorDecl>(decl))
                            source = tu->GetObject(a, des, clang::CodeGen::StructorType::Complete);
                        else if (auto con = llvm::dyn_cast<clang::CXXConstructorDecl>(decl))
                            source = tu->GetObject(a, con, clang::CodeGen::StructorType::Complete);
                        else
                            source = tu->GetObject(a, decl);
                        if (auto mem = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                            exported_this = true;
                        }
                        clang_exports.push_back(std::make_tuple(source, GetFunctionType(decl, *tu, a), decl, attr.initializer->location));
                    }
                }
            }
        }
    }
    assert(mem);
    // Only match the non-concrete arguments.
    root_scope = Wide::Memory::MakeUnique<Scope>(nullptr);
    unsigned num = 0;
    // We might still be a member function if we're exported as one later.         
    auto Parameter = [this, &a, exported_this](unsigned num, Lexer::Range where) -> std::shared_ptr<Expression> {
        // Did we have an exported this?
        Type* root_ty = analyzer.GetFunctionParameters(GetASTFunction(), context)[num];
        assert(!key || num < key->size());
        auto get_new_ty = [&]() -> Type* {
            if (root_ty->IsReference())
                return a.GetLvalueType(root_ty->Decay()); // Is this wrong in the case of named rvalue reference?
            return a.GetLvalueType(root_ty);
        };
        return CreatePrimGlobal(Range::Empty(), get_new_ty(), [=](CodegenContext& con) -> llvm::Value* {
            auto argnum = num;
            // We have an extra argument for the return type if we have 1 more argument than the number in the key.
            if (analyzer.GetWideFunction(this)->GetSignature()->GetReturnType()->AlwaysKeepInMemory(con))
                ++argnum;
            auto llvm_argument = std::next(con->GetInsertBlock()->getParent()->arg_begin(), argnum);

            if (root_ty->AlwaysKeepInMemory(con) || root_ty->IsReference())
                return llvm_argument;

            auto alloc = con.CreateAlloca(root_ty);
            con->CreateStore(llvm_argument, alloc);
            return alloc;
        });
    };

    if (GetNonstaticMemberContext()) {
        auto param = Parameter(num++, Lexer::Range(nullptr));
        root_scope->named_variables.insert(std::make_pair("this", std::make_pair(param, Lexer::Range(nullptr))));
        root_scope->active.push_back(param);
        parameters.push_back(param);
    }
    for (auto&& arg : astfun->args) {
        if (arg.name == "this")
            continue;
        auto param = Parameter(num++, arg.location);
        root_scope->named_variables.insert(std::make_pair(arg.name, std::make_pair(param, arg.location)));
        root_scope->active.push_back(param);
        parameters.push_back(param);
    }
}
Type* FunctionSkeleton::GetExplicitReturn() {
    // Explicit return type, if any
    if (auto fun = dynamic_cast<const Parse::Function*>(this->fun)) {
        if (fun->explicit_return) {
            auto expr = analyzer.AnalyzeExpression(context, fun->explicit_return.get(), nullptr);
            if (auto con = dynamic_cast<ConstructorType*>(expr->GetType()->Decay())) {
                return con->GetConstructedType();
            } else {
                // Try and let function return type inference handle it.
                ExplicitReturnError = Wide::Memory::MakeUnique<Semantic::SpecificError<ExplicitReturnNoType>>(analyzer, fun->explicit_return->location, "Explicit return type was not a type.");
            }
        }
    }
    // Constructors and destructors, we know in advance to return void.
    if (dynamic_cast<const Parse::Constructor*>(fun) || dynamic_cast<const Parse::Destructor*>(fun)) {
        return analyzer.GetVoidType();
    }
    return nullptr;
}

void FunctionSkeleton::ComputeBody() {
    for (std::size_t i = 0; i < fun->statements.size(); ++i) {
        root_scope->active.push_back(AnalyzeStatement(analyzer, this, fun->statements[i].get(), GetLocalContext(), LookupLocal("this")));
    }
}

Location FunctionSkeleton::GetLocalContext() {
    return Location(context, root_scope.get());
}

Scope* FunctionSkeleton::EnsureComputedBody() {
    if (!analyzed) {
        ComputeBody();
        analyzed = true;
    }
    return root_scope.get();
}

std::shared_ptr<Expression> FunctionSkeleton::LookupLocal(Parse::Name name) {
    Scope* current_scope = root_scope.get();
    while (!current_scope->children.empty())
        current_scope = current_scope->children.back().get();
    if (auto string = boost::get<std::string>(&name.name))
        return current_scope->LookupLocal(*string);
    return nullptr;
}

void FunctionSkeleton::AddDefaultHandlers(Analyzer& a) {
    AddHandler<const Parse::Return>(a.StatementHandlers, [](const Parse::Return* ret, FunctionSkeleton* skel, Analyzer& analyzer, Location l, std::shared_ptr<Expression> _this) {
        auto ret_expr = ret->RetExpr ? analyzer.AnalyzeExpression(l, ret->RetExpr.get(), _this) : nullptr;
        return CreateResultExpression(Range::Elements(ret_expr), [=, &analyzer]() -> std::shared_ptr<Expression> {
            auto func = analyzer.GetWideFunction(skel);
            func->AddReturnExpression(ret_expr.get());
            if (!ret_expr) return CreatePrimGlobal(Range::Empty(), analyzer, [](CodegenContext& con) {});
            std::shared_ptr<std::shared_ptr<Expression>> build = std::make_shared<std::shared_ptr<Expression>>();
            func->ReturnTypeChanged.connect([=, &analyzer](Type* t) {
                if (t != analyzer.GetVoidType() && t) {
                    *build = Type::BuildInplaceConstruction(
                        CreatePrimGlobal(Range::Elements(ret_expr), analyzer.GetLvalueType(t), [](CodegenContext& con) { return con->GetInsertBlock()->getParent()->arg_begin(); }),
                        { ret_expr }, { l, ret->location }
                    );
                    (*build)->GetType();
                }
            });
            return CreatePrimGlobal(Range::Elements(ret_expr), analyzer, [=, &analyzer](CodegenContext& con) {
                if (func->GetSignature()->GetReturnType() == analyzer.GetVoidType()) {
                    // If we have a void-returning expression, evaluate it, destroy it, then return.
                    if (ret_expr) {
                        ret_expr->GetValue(con);
                    }
                    con.DestroyAll(false);
                    con->CreateRetVoid();
                    return;
                }

                // If we return a simple type
                if (!func->GetSignature()->GetReturnType()->AlwaysKeepInMemory(con)) {
                    // and we already have an expression of that type
                    if (ret_expr->GetType() == func->GetSignature()->GetReturnType()) {
                        // then great, just return it directly.
                        auto val = ret_expr->GetValue(con);
                        con.DestroyAll(false);
                        con->CreateRet(val);
                        return;
                    }
                    // If we have a reference to it, just load it right away.
                    if (ret_expr->GetType()->IsReference(func->GetSignature()->GetReturnType())) {
                        auto val = con->CreateLoad(ret_expr->GetValue(con));
                        con.DestroyAll(false);
                        con->CreateRet(val);
                        return;
                    }
                    // Build will inplace construct this in our first argument, which is INCREDIBLY UNHELPFUL here.
                    // We would fix this up, but, cannot query the complexity of a type prior to code generation.
                    auto val = func->GetSignature()->GetReturnType()->BuildValueConstruction({ ret_expr }, { l, ret->RetExpr->location })->GetValue(con);
                    con.DestroyAll(false);
                    con->CreateRet(val);
                    return;
                }

                // If we return a complex type, the 0th parameter will be memory into which to place the return value.
                // build should be a function taking the memory and our ret's value and emplacing it.
                // Then return void.
                (*build)->GetValue(con);
                con.DestroyAll(false);
                con->CreateRetVoid();
                return;
            });
        });
    });

    AddHandler<const Parse::CompoundStatement>(a.StatementHandlers, [](const Parse::CompoundStatement* comp, FunctionSkeleton* skel, Analyzer& analyzer, Location self, std::shared_ptr<Expression> _this) {
        auto compound = new Scope(self.localscope);
        for (auto&& stmt : comp->stmts)
            compound->active.push_back(AnalyzeStatement(analyzer, skel, stmt.get(), Location(self, compound), _this));
        return CreatePrimGlobal(Range::Empty(), analyzer, [=](CodegenContext& con) {
            con.GenerateCodeAndDestroyLocals([=](CodegenContext& con) {
                for (auto&& stmt : compound->active)
                    if (!con.IsTerminated(con->GetInsertBlock()))
                        stmt->GenerateCode(con);
            });
        });
    });

    AddHandler<const Parse::Variable>(a.StatementHandlers, [](const Parse::Variable* var, FunctionSkeleton* skel, Analyzer& analyzer, Location self, std::shared_ptr<Expression> _this) {
        auto current = self.localscope;
        auto init_expr = analyzer.AnalyzeExpression(self, var->initializer.get(), _this);
        auto var_type_expr = var->type ? analyzer.AnalyzeExpression(self, var->type.get(), _this) : nullptr;
        if (var->name.size() == 1) {
            auto&& name = var->name.front();
            if (current->named_variables.find(var->name.front().name) != current->named_variables.end())
                Semantic::AddError<VariableAlreadyDefined>(analyzer, var, name.where, "Variable has same name as another in current scope.");
            auto var_init = CreateResultExpression(Range::Elements(init_expr, var_type_expr), [=, &analyzer]() {
                Type* var_type = nullptr;
                if (var_type_expr) {
                    auto conty = dynamic_cast<ConstructorType*>(var_type_expr->GetType()->Decay());
                    if (!conty) throw std::runtime_error("Local variable type was not a type.");
                    var_type = conty->GetConstructedType();
                } else
                    var_type = init_expr->GetType()->Decay();
                if (var_type == init_expr->GetType() && var_type->IsReference())
                    return CreatePrimGlobal(Range::Elements(init_expr), var_type, [=](CodegenContext& con) -> llvm::Value* {
                        return init_expr->GetValue(con);
                    });
                if (var_type == init_expr->GetType())
                    return CreatePrimGlobal(Range::Elements(init_expr), analyzer.GetLvalueType(var_type), [=](CodegenContext& con) -> llvm::Value* {
                        if (init_expr->GetType()->AlwaysKeepInMemory(con)) return init_expr->GetValue(con);
                        auto alloc = con.CreateAlloca(var_type);
                        con->CreateStore(init_expr->GetValue(con), alloc);
                        return alloc;
                    });
                auto temp = CreateTemporary(var_type, { self, name.where });
                auto construct = Type::BuildInplaceConstruction(temp, { init_expr }, { self, var->initializer.get()->location });
                auto des = var_type->BuildDestructorCall(temp, { self, name.where }, true);
                return CreatePrimGlobal(Range::Elements(construct), analyzer.GetLvalueType(var_type), [=](CodegenContext& con) {
                    construct->GetValue(con);
                    con.AddDestructor(des);
                    return temp->GetValue(con);
                });
            });
            current->named_variables.insert(std::make_pair(name.name, std::make_pair(var_init, name.where)));
            return var_init;
        }
        std::vector<std::shared_ptr<Expression>> exprs;
        for (auto&& name : var->name) {
            auto offset = &name - &var->name[0];
            if (current->named_variables.find(name.name) != current->named_variables.end())
                Semantic::AddError<VariableAlreadyDefined>(analyzer, var, name.where, "Variable has same name as another in current scope.");
            auto local = CreateResultExpression(Range::Elements(init_expr, var_type_expr), [=, &analyzer]() {
                Type* var_type = nullptr;
                std::shared_ptr<Expression> local_init;
                if (var_type_expr) {
                    auto conty = dynamic_cast<ConstructorType*>(var_type_expr->GetType()->Decay());
                    if (!conty) throw std::runtime_error("Local variable type was not a type.");
                    auto tup = dynamic_cast<TupleType*>(conty->GetConstructedType());
                    if (!tup || tup->GetMembers().size() != var->name.size())
                        throw std::runtime_error("Tuple size and name number mismatch.");
                    var_type = tup->GetMembers()[offset];
                    local_init = tup->PrimitiveAccessMember(init_expr, offset);
                } else {
                    auto tup = dynamic_cast<TupleType*>(init_expr->GetType()->Decay());
                    if (!tup || tup->GetMembers().size() != var->name.size())
                        throw std::runtime_error("Tuple size and name number mismatch.");
                    var_type = tup->GetMembers()[offset]->Decay();
                    local_init = tup->PrimitiveAccessMember(init_expr, offset);
                }
                auto temp = CreateTemporary(var_type, { self, name.where });
                auto construct = Type::BuildInplaceConstruction(temp, { local_init }, { self, name.where });
                auto des = var_type->BuildDestructorCall(temp, { self, name.where }, true);
                return CreatePrimGlobal(Range::Elements(construct), analyzer.GetLvalueType(var_type), [=](CodegenContext& con) {
                    construct->GetValue(con);
                    con.AddDestructor(des);
                    return temp->GetValue(con);
                });
            });
            current->named_variables.insert(std::make_pair(name.name, std::make_pair(local, name.where)));
            exprs.push_back(local);
        }
        return CreateResultExpression(Range::Elements(init_expr, var_type_expr) | Range::Concat(Range::Container(exprs)), [=, &analyzer]() {
            return CreatePrimGlobal(Range::Container(exprs), analyzer, [=](CodegenContext& con) {
                for (auto&& var : exprs)
                    var->GetValue(con);
            });
        });
    });

    AddHandler<const Parse::While>(a.StatementHandlers, [](const Parse::While* whil, FunctionSkeleton* skel, Analyzer& analyzer, Location self, std::shared_ptr<Expression> _this) -> std::shared_ptr<Statement> {
        auto current = self.localscope;
        struct WhileStatement : Statement, ControlFlowStatement {
            Lexer::Range where;
            std::shared_ptr<Statement> body;
            std::shared_ptr<Expression> boolconvert;
            llvm::BasicBlock* continue_bb = nullptr;
            llvm::BasicBlock* check_bb = nullptr;
            CodegenContext* source_con = nullptr;
            CodegenContext* condition_con = nullptr;

            WhileStatement(std::shared_ptr<Expression> ex, Lexer::Range where)
                : where(where)
            {
                boolconvert = ex;
            }
            void GenerateCode(CodegenContext& con) override final {
                source_con = &con;
                check_bb = llvm::BasicBlock::Create(con, "check_bb", con->GetInsertBlock()->getParent());
                auto loop_bb = llvm::BasicBlock::Create(con, "loop_bb", con->GetInsertBlock()->getParent());
                continue_bb = llvm::BasicBlock::Create(con, "continue_bb", con->GetInsertBlock()->getParent());
                con->CreateBr(check_bb);
                con->SetInsertPoint(check_bb);
                // Both of these branches need to destroy the cond's locals.
                // In the case of the loop, so that when check_bb is re-entered, it's clear.
                // In the case of the continue, so that the check's locals are cleaned up properly.
                CodegenContext condition_context(con);
                condition_con = &condition_context;
                auto condition = boolconvert->GetValue(condition_context);
                if (condition->getType() == llvm::Type::getInt8Ty(condition_context))
                    condition = condition_context->CreateTrunc(condition, llvm::Type::getInt1Ty(condition_context));
                condition_context->CreateCondBr(condition, loop_bb, continue_bb);
                condition_context->SetInsertPoint(loop_bb);
                condition_context.GenerateCodeAndDestroyLocals([this](CodegenContext& body_context) {
                    body->GenerateCode(body_context);
                });
                // If, for example, we unconditionally return or break/continue, it can happen that we were already terminated.
                // We expect that, in the continue BB, the check's locals are alive.
                if (!con.IsTerminated(con->GetInsertBlock())) {
                    con.DestroyDifference(condition_context, false);
                    con->CreateBr(check_bb);
                }
                con->SetInsertPoint(continue_bb);
                con.DestroyDifference(condition_context, false);
                condition_con = nullptr;
                source_con = nullptr;
            }
            void JumpForContinue(CodegenContext& con) {
                source_con->DestroyDifference(con, false);
                con->CreateBr(check_bb);
            }
            void JumpForBreak(CodegenContext& con) {
                condition_con->DestroyDifference(con, false);
                con->CreateBr(continue_bb);
            }
            void Instantiate() override final {
                body->Instantiate();
                boolconvert->GetType();
            }
        };
        auto condscope = new Scope(current);
        auto get_expr = [&, self]() -> std::shared_ptr<Expression> {
            if (whil->var_condition) {
                if (whil->var_condition->name.size() != 1)
                    throw std::runtime_error("fuck");
                condscope->active.push_back(AnalyzeStatement(analyzer, skel, whil->var_condition.get(), Location(self, condscope), _this));
                return condscope->named_variables.begin()->second.first;
            }
            return analyzer.AnalyzeExpression(self, whil->condition.get(), _this);
        };
        auto ex = get_expr();
        auto cond = CreateResultExpression(Range::Elements(ex), [=]() {
            return Type::BuildBooleanConversion(ex, Context(self, whil->location));
        });
        auto while_stmt = std::make_shared<WhileStatement>(cond, whil->location);
        condscope->active.push_back(std::move(cond));
        condscope->control_flow = while_stmt.get();
        auto bodyscope = new Scope(condscope);
        bodyscope->active.push_back(AnalyzeStatement(analyzer, skel, whil->body.get(), Location(self, bodyscope), _this));
        while_stmt->body = bodyscope->active.back();
        return while_stmt;
    });

    AddHandler<const Parse::Continue>(a.StatementHandlers, [](const Parse::Continue* continue_stmt, FunctionSkeleton* skel, Analyzer& analyzer, Location self, std::shared_ptr<Expression> _this) {
        auto flow = self.localscope->GetCurrentControlFlow();
        if (!flow)
            Semantic::AddError<ContinueNoControlFlowStatement>(analyzer, continue_stmt, "Continue statement with no control flow statement to continue.");
        return CreatePrimGlobal(Range::Empty(), analyzer, [=](CodegenContext& con) {
            flow->JumpForContinue(con);
        });
    });

    AddHandler<const Parse::Break>(a.StatementHandlers, [](const Parse::Break* break_stmt, FunctionSkeleton* skel, Analyzer& analyzer, Location self, std::shared_ptr<Expression> _this) {
        auto flow = self.localscope->GetCurrentControlFlow();
        if (!flow)
            Semantic::AddError<BreakNoControlFlowStatement>(analyzer, break_stmt, "Continue statement with no control flow statement to continue.");
        return CreatePrimGlobal(Range::Empty(), analyzer, [=](CodegenContext& con) {
            flow->JumpForBreak(con);
        });
    });

    AddHandler<const Parse::If>(a.StatementHandlers, [](const Parse::If* if_stmt, FunctionSkeleton* skel, Analyzer& analyzer, Location self, std::shared_ptr<Expression> _this) {
        auto condscope = new Scope(self.localscope);
        auto get_expr = [&, self]() -> std::shared_ptr<Expression> {
            if (if_stmt->var_condition) {
                if (if_stmt->var_condition->name.size() != 1)
                    throw std::runtime_error("fuck");
                condscope->active.push_back(AnalyzeStatement(analyzer, skel, if_stmt->var_condition.get(), Location(self, condscope), _this));
                return condscope->named_variables.begin()->second.first;
            }
            return analyzer.AnalyzeExpression(self, if_stmt->condition.get(), _this);
        };
        auto cond_expr = get_expr();
        auto cond = CreateResultExpression(Range::Elements(cond_expr), [=]() {
            return Type::BuildBooleanConversion(cond_expr, { self, if_stmt->location });
        });
        condscope->active.push_back(cond);
        Statement* true_br = nullptr;
        {
            auto truescope = new Scope(condscope);
            truescope->active.push_back(AnalyzeStatement(analyzer, skel, if_stmt->true_statement.get(), Location(self, truescope), _this));
            true_br = truescope->active.back().get();
        }
        Statement* false_br = nullptr;
        if (if_stmt->false_statement) {
            auto falsescope = new Scope(condscope);
            falsescope->active.push_back(AnalyzeStatement(analyzer, skel, if_stmt->false_statement.get(), Location(self, falsescope), _this));
            false_br = falsescope->active.back().get();
        }
        return CreatePrimGlobal(Range::Elements(cond), analyzer, [=](CodegenContext& con) {
            auto true_bb = llvm::BasicBlock::Create(con, "true_bb", con->GetInsertBlock()->getParent());
            auto continue_bb = llvm::BasicBlock::Create(con, "continue_bb", con->GetInsertBlock()->getParent());
            auto else_bb = false_br ? llvm::BasicBlock::Create(con->getContext(), "false_bb", con->GetInsertBlock()->getParent()) : continue_bb;
            con.GenerateCodeAndDestroyLocals([=](CodegenContext& condition_con) {
                auto condition = cond->GetValue(condition_con);
                if (condition->getType() == llvm::Type::getInt8Ty(condition_con))
                    condition = condition_con->CreateTrunc(condition, llvm::Type::getInt1Ty(condition_con));
                condition_con->CreateCondBr(condition, true_bb, else_bb);
                condition_con->SetInsertPoint(true_bb);
                condition_con.GenerateCodeAndDestroyLocals([=](CodegenContext& true_con) {
                    true_br->GenerateCode(true_con);
                });
                if (!condition_con.IsTerminated(condition_con->GetInsertBlock()))
                    condition_con->CreateBr(continue_bb);
                if (false_br) {
                    condition_con->SetInsertPoint(else_bb);
                    condition_con.GenerateCodeAndDestroyLocals([=](CodegenContext& false_con) {
                        false_br->GenerateCode(false_con);
                    });
                    if (!condition_con.IsTerminated(condition_con->GetInsertBlock()))
                        condition_con->CreateBr(continue_bb);
                }
                condition_con->SetInsertPoint(continue_bb);
            });
        });
    });

    AddHandler<const Parse::Throw>(a.StatementHandlers, [](const Parse::Throw* thro, FunctionSkeleton* skel, Analyzer& analyzer, Location self, std::shared_ptr<Expression> _this) {
        if (!thro->expr) return CreatePrimGlobal(Range::Empty(), analyzer, [=](CodegenContext& con) {
            if (con.HasDestructors() || con.EHHandler)
                con->CreateInvoke(con.GetCXARethrow(), con.GetUnreachableBlock(), con.CreateLandingpadForEH());
            else
                con->CreateCall(con.GetCXARethrow());
        });
        auto expr = analyzer.AnalyzeExpression(self, thro->expr.get(), _this);
        return CreateResultExpression(Range::Elements(expr), [=, &analyzer]() {
            return CreatePrimGlobal(Range::Empty(), analyzer, Semantic::ThrowObject(expr, { self, thro->location }));
        });        
    });

    AddHandler<const Parse::TryCatch>(a.StatementHandlers, [](const Parse::TryCatch* try_, FunctionSkeleton* skel, Analyzer& analyzer, Location self, std::shared_ptr<Expression> _this) {
        auto current = self.localscope;
        struct Catch {
            Scope* cscope;
            std::shared_ptr<llvm::Value*> val;
            std::shared_ptr<Expression> type;
            std::function<llvm::Constant*(llvm::Module*)> RTTI;
        };
        auto tryscope = new Scope(current);
        for (auto&& stmt : try_->statements->stmts)
            tryscope->active.push_back(AnalyzeStatement(analyzer, skel, stmt.get(), Location(self, tryscope), _this));
        std::vector<Catch> catches;
        for (auto&& catch_ : try_->catches) {
            auto catchscope = new Scope(current);
            if (catch_.all) {
                std::vector<std::shared_ptr<Statement>> stmts;
                for (auto&& stmt : catch_.statements)
                    stmts.push_back(AnalyzeStatement(analyzer, skel, stmt.get(), Location(self, catchscope), _this));
                catchscope->active = std::move(stmts);
                catches.push_back(Catch{ catchscope });
                break;
            }
            auto type = analyzer.AnalyzeExpression(Location(self, catchscope), catch_.type.get(), _this);
            auto param = std::make_shared<llvm::Value*>();
            auto catch_param = CreateResultExpression(Range::Elements(type), [=]() {
                auto con = dynamic_cast<ConstructorType*>(type->GetType());
                if (!con) throw std::runtime_error("Catch parameter type was not a type.");
                auto catch_type = con->GetConstructedType();
                if (!IsLvalueType(catch_type) && !dynamic_cast<PointerType*>(catch_type))
                    throw std::runtime_error("Attempted to catch by non-lvalue and nonpointer.");
                return CreatePrimGlobal(Range::Elements(type), catch_type, [=](CodegenContext& con) {
                    return con->CreatePointerCast(*param, catch_type->GetLLVMType(con));
                });
            });
            catchscope->named_variables.insert(std::make_pair(catch_.name, std::make_pair(catch_param, catch_.type->location)));
            std::vector<std::shared_ptr<Statement>> stmts;
            for (auto&& stmt : catch_.statements)
                stmts.push_back(AnalyzeStatement(analyzer, skel, stmt.get(), Location(self, catchscope), _this));
            catchscope->active = std::move(stmts);
            catches.push_back(Catch{ catchscope, param, type });
        }
        return CreateResultExpression(Range::Container(catches) | Range::Map([](Catch cat) { return cat.type; }), [=, &analyzer]() {
            auto catch_blocks = catches;
            for (auto&& catch_ : catch_blocks) {
                if (!catch_.type) continue;
                auto con = dynamic_cast<ConstructorType*>(catch_.type->GetType());
                if (!con) throw std::runtime_error("Catch parameter type was not a type.");
                auto catch_type = con->GetConstructedType();
                if (!IsLvalueType(catch_type) && !dynamic_cast<PointerType*>(catch_type))
                    throw std::runtime_error("Attempted to catch by non-lvalue and nonpointer.");
                auto target_type = IsLvalueType(catch_type)
                    ? catch_type->Decay()
                    : dynamic_cast<PointerType*>(catch_type)->GetPointee();
                catch_.RTTI = target_type->GetRTTI();
                assert(catch_.RTTI);
            }
            return CreatePrimGlobal(Range::Empty(), analyzer, [=](CodegenContext& con) {
                auto source_block = con->GetInsertBlock();

                auto try_con = con;
                auto catch_block = llvm::BasicBlock::Create(con, "catch_block", con->GetInsertBlock()->getParent());
                auto dest_block = llvm::BasicBlock::Create(con, "dest_block", con->GetInsertBlock()->getParent());

                con->SetInsertPoint(catch_block);
                auto phi = con->CreatePHI(con.GetLpadType(), 0);
                std::vector<llvm::Constant*> rttis;
                for (auto&& catch_ : catch_blocks) {
                    if (catch_.val)
                        rttis.push_back(llvm::cast<llvm::Constant>(con->CreatePointerCast(catch_.RTTI(con), con.GetInt8PtrTy())));
                }
                try_con.EHHandler = CodegenContext::EHScope{ &con, catch_block, phi, rttis };
                try_con->SetInsertPoint(source_block);
                try_con.GenerateCodeAndDestroyLocals([=](CodegenContext& trycon) {
                    for (auto&& stmt : tryscope->active)
                        if (!trycon.IsTerminated(trycon->GetInsertBlock()))
                            stmt->GenerateCode(trycon);
                });
                if (!try_con.IsTerminated(try_con->GetInsertBlock()))
                    try_con->CreateBr(dest_block);
                if (phi->getNumIncomingValues() == 0) {
                    phi->removeFromParent();
                    catch_block->removeFromParent();
                    con->SetInsertPoint(dest_block);
                    return;
                }

                // Generate the code for all the catch statements.
                auto for_ = llvm::Intrinsic::getDeclaration(con, llvm::Intrinsic::eh_typeid_for);
                auto catch_con = con;
                catch_con->SetInsertPoint(catch_block);
                auto selector = catch_con->CreateExtractValue(phi, { 1 });
                auto except_object = catch_con->CreateExtractValue(phi, { 0 });

                auto catch_ender = [](CodegenContext& con) {
                    con->CreateCall(con.GetCXAEndCatch());
                };
                for (auto&& catch_ : catch_blocks) {
                    if (!catch_.val) {
                        catch_con.GenerateCodeAndDestroyLocals([&](CodegenContext& catch_block_con) {
                            for (auto&& stmt : catch_.cscope->active)
                                if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock()))
                                    stmt->GenerateCode(catch_block_con);
                        });
                        if (!catch_con.IsTerminated(catch_con->GetInsertBlock()))
                            catch_con->CreateBr(dest_block);
                        continue;
                    }
                    auto catch_target = llvm::BasicBlock::Create(con, "catch_target", catch_con->GetInsertBlock()->getParent());
                    auto catch_continue = llvm::BasicBlock::Create(con, "catch_continue", catch_con->GetInsertBlock()->getParent());
                    auto target_selector = catch_con->CreateCall(for_, { con->CreatePointerCast(catch_.RTTI(con), con.GetInt8PtrTy()) });
                    auto result = catch_con->CreateICmpEQ(selector, target_selector);
                    catch_con->CreateCondBr(result, catch_target, catch_continue);
                    catch_con->SetInsertPoint(catch_target);
                    // Call __cxa_begin_catch and get our result. We don't need __cxa_get_exception_ptr as Wide cannot catch by value.
                    *catch_.val = catch_con->CreateCall(catch_con.GetCXABeginCatch(), { except_object });
                    // Ensure __cxa_end_catch is called.

                    catch_con.GenerateCodeAndDestroyLocals([&](CodegenContext& catch_block_con) {
                        catch_block_con.AddDestructor(catch_ender);
                        for (auto&& stmt : catch_.cscope->active)
                            if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock()))
                                stmt->GenerateCode(catch_block_con);
                    });
                    if (!catch_con.IsTerminated(catch_con->GetInsertBlock()))
                        catch_con->CreateBr(dest_block);
                    catch_con->SetInsertPoint(catch_continue);
                }
                // If we had no catch all, then we need to clean up and rethrow to the next try.
                if (catch_blocks.back().val) {
                    auto except = catch_con->CreateCall(catch_con.GetCXABeginCatch(), { except_object });
                    catch_con.AddDestructor(catch_ender);
                    catch_con->CreateInvoke(catch_con.GetCXARethrow(), catch_con.GetUnreachableBlock(), catch_con.CreateLandingpadForEH());
                }
                con->SetInsertPoint(dest_block);
            });
        });
    });
}
FunctionSkeleton::~FunctionSkeleton() {}
std::vector<std::tuple<std::function<llvm::Function*(llvm::Module*)>, ClangFunctionType*, clang::FunctionDecl*, Lexer::Range>>& FunctionSkeleton::GetClangExports() {
    return clang_exports;
}

Type* FunctionSkeleton::GetNonstaticMemberContext() {
    return GetNonstaticContext(context);
}