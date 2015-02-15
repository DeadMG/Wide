#include <Wide/Semantic/FunctionSkeleton.h>
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
#include <Wide/Semantic/Function.h>
#include <unordered_set>
#include <sstream>
#include <iostream>

#pragma warning(push, 0)
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/raw_os_ostream.h>
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

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

FunctionSkeleton::FunctionSkeleton(const Parse::FunctionBase* astfun, Analyzer& a, Type* mem, std::string src_name, Type* nonstatic_context, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup)
    : fun(astfun)
    , context(mem)
    , current_state(State::NotYetAnalyzed)
    , root_scope(nullptr)
    , source_name(src_name)
    , analyzer(a)
    , NonstaticLookup(NonstaticLookup) {
    assert(mem);
    // Only match the non-concrete arguments.
    root_scope = Wide::Memory::MakeUnique<Scope>(nullptr);
    unsigned num = 0;
    // We might still be a member function if we're exported as one later.         
    auto Parameter = [&a](unsigned num, Lexer::Range where) {
        return CreateResultExpression(Range::Empty(), [=, &a](Expression::InstanceKey key) -> std::shared_ptr<Expression> {
            if (!key) return nullptr;
            auto get_new_ty = [&]() -> Type* {
                auto root_ty = Expression::GetArgumentType(key, num);
                if (root_ty->IsReference())
                    return a.GetLvalueType(root_ty->Decay()); // Is this wrong in the case of named rvalue reference?
                return a.GetLvalueType(root_ty);
            };
            return CreatePrimGlobal(Range::Empty(), get_new_ty(), [=](CodegenContext& con) -> llvm::Value* {
                auto argnum = num;
                // We have an extra argument for the return type if we have 1 more argument than the number in the key.
                if (con->GetInsertBlock()->getParent()->arg_size() == key->size() + 1)
                    ++argnum;
                auto llvm_argument = std::next(con->GetInsertBlock()->getParent()->arg_begin(), argnum);

                if (Expression::GetArgumentType(key, num)->AlwaysKeepInMemory(con) || Expression::GetArgumentType(key, num)->IsReference())
                    return llvm_argument;

                auto alloc = con.CreateAlloca(Expression::GetArgumentType(key, num));
                con->CreateStore(llvm_argument, alloc);
                return alloc;
            });
        });
    };
    if (nonstatic_context) {
        NonstaticMemberContext = nonstatic_context;
        if (auto con = dynamic_cast<Semantic::ConstructorContext*>(nonstatic_context))
            ConstructorContext = con;
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
Type* FunctionSkeleton::GetExplicitReturn(Expression::InstanceKey key) {
    // Explicit return type, if any
    if (auto fun = dynamic_cast<const Parse::Function*>(this->fun)) {
        if (fun->explicit_return) {
            auto expr = analyzer.AnalyzeExpression(GetContext(), fun->explicit_return.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
            if (auto con = dynamic_cast<ConstructorType*>(expr->GetType(Expression::NoInstance())->Decay())) {
                return con->GetConstructedType();
            } else
                throw NotAType(expr->GetType(Expression::NoInstance()), fun->explicit_return->location);
        }
    }
    // Constructors and destructors, we know in advance to return void.
    if (dynamic_cast<const Parse::Constructor*>(fun) || dynamic_cast<const Parse::Destructor*>(fun)) {
        return analyzer.GetVoidType();
    }
    return nullptr;
}

Scope* FunctionSkeleton::ComputeBody() {
    if (current_state == State::NotYetAnalyzed) {
        current_state = State::AnalyzeInProgress;
        // Initializers first, if we are a constructor, then set virtual pointers.        
        if (auto con = dynamic_cast<const Parse::Constructor*>(fun)) {
            if (!ConstructorContext) throw std::runtime_error("fuck");
            auto member = *ConstructorContext;
            auto members = member->GetConstructionMembers();
            auto make_member = [this](Type* result, std::function<unsigned()> offset, std::shared_ptr<Expression> self) {
                return CreatePrimUnOp(self, result, [offset, result](llvm::Value* val, CodegenContext& con) -> llvm::Value* {
                    auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
                    self = con->CreateConstGEP1_32(self, offset());
                    return con->CreatePointerCast(self, result->GetLLVMType(con));
                });
            };
            auto SetVirtualPointers = [=] {
                auto this_ = LookupLocal("this");
                return CreateResultExpression(Range::Elements(this_), [=](Expression::InstanceKey key) {
                    return Type::SetVirtualPointers(key, this_);
                });
            };
            auto BuildInplaceConstruction = [=](std::shared_ptr<Expression> where, std::vector<std::shared_ptr<Expression>> inits, Context c) {
                return CreateResultExpression(Range::Elements(where) | Range::Concat(Range::Container(inits)), [=](Expression::InstanceKey key) {
                    return Type::BuildInplaceConstruction(key, where, inits, c);
                });
            };
            // Are we defaulted?
            if (con->defaulted) {
                // Only accept some arguments.
                if (con->args.size() == 0) {
                    // Default-or-NSDMI all the things.
                    for (auto&& x : members) {
                        auto member = make_member(analyzer.GetLvalueType(x.t), x.num, LookupLocal("this"));
                        std::vector<std::shared_ptr<Expression>> inits;
                        if (x.InClassInitializer)
                            inits = { x.InClassInitializer(LookupLocal("this")) };
                        root_scope->active.push_back(BuildInplaceConstruction(member, inits, { GetContext(), con->where }));
                    }
                    root_scope->active.push_back(SetVirtualPointers());
                } 
                // A single-argument constructor.
                auto argcon = analyzer.AnalyzeExpression(GetContext(), con->args[0].type.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
                if (auto argconty = dynamic_cast<ConstructorType*>(argcon->GetType(Expression::NoInstance()))) {
                    if (argconty->GetConstructedType() == analyzer.GetLvalueType(GetContext())) {
                        // Copy constructor- copy all.
                        unsigned i = 0;
                        for (auto&& x : members) {
                            auto member_ref = make_member(analyzer.GetLvalueType(x.t), x.num, LookupLocal("this"));
                            root_scope->active.push_back(BuildInplaceConstruction(member_ref, { member->PrimitiveAccessMember(parameters[1], i++) }, { GetContext(), con->where }));
                        }
                        root_scope->active.push_back(SetVirtualPointers());
                    } else if (argconty->GetConstructedType() == analyzer.GetRvalueType(GetContext())) {
                        // move constructor- move all.
                        unsigned i = 0;
                        for (auto&& x : members) {
                            auto member_ref = make_member(analyzer.GetLvalueType(x.t), x.num, LookupLocal("this"));
                            root_scope->active.push_back(BuildInplaceConstruction(member_ref, { member->PrimitiveAccessMember(std::make_shared<RvalueCast>(parameters[1]), i++) }, { GetContext(), con->where }));
                        }
                        root_scope->active.push_back(SetVirtualPointers());
                    } else
                        throw std::runtime_error("Fuck");
                } else
                    throw std::runtime_error("Fuck");
            } else {
                // Are we delegating?
                auto is_delegating = [this, con] {
                    if (con->initializers.size() != 1)
                        return false;
                    if (auto ident = dynamic_cast<const Parse::Identifier*>(con->initializers.front().initialized.get())) {
                        if (ident->val == decltype(ident->val)("type")) {
                            return true;
                        }
                    }
                    return false;
                };
                if (is_delegating()) {
                    Context c{ GetContext(), con->initializers.front().where };
                    auto expr = analyzer.AnalyzeExpression(GetContext(), con->initializers.front().initializer.get(), root_scope.get(), NonstaticLookup);
                    auto conty = dynamic_cast<Type*>(*ConstructorContext);
                    auto conoverset = conty->GetConstructorOverloadSet(Parse::Access::Private);
                    root_scope->active.push_back(CreateResultExpression(Range::Elements(expr), [=](Expression::InstanceKey key) {
                        auto destructor = conty->BuildDestructorCall(key, LookupLocal("this"), c, true);
                        auto self = LookupLocal("this");
                        auto callable = conoverset->Resolve({ self->GetType(key), expr->GetType(key) }, GetContext());
                        if (!callable) throw std::runtime_error("Couldn't resolve delegating constructor call.");
                        auto call = callable->Call(key, { LookupLocal("this"), expr }, c);
                        return BuildChain(call, CreatePrimGlobal(Range::Elements(self, expr), analyzer.GetVoidType(), [=](CodegenContext& con) {
                            if (!self->GetType(key)->Decay()->IsTriviallyDestructible())
                                con.AddExceptionOnlyDestructor(destructor);
                            return nullptr;
                        }));
                    }));
                    // Don't bother setting virtual pointers as the delegated-to constructor already did.
                } else {
                    std::unordered_set<const Parse::VariableInitializer*> used_initializers;
                    for (auto&& x : members) {
                        auto has_initializer = [&]() -> const Parse::VariableInitializer*{
                            for (auto&& init : con->initializers) {
                                // Match if it's a name and the one we were looking for.
                                auto ident = dynamic_cast<const Parse::Identifier*>(init.initialized.get());
                                if (x.name && ident) {
                                    if (auto string = boost::get<std::string>(&ident->val))
                                        if (*string == *x.name)
                                            return &init;
                                } else {
                                    // Match if it's a type and the one we were looking for.
                                    auto ty = analyzer.AnalyzeExpression(GetContext(), init.initialized.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
                                    if (auto conty = dynamic_cast<ConstructorType*>(ty->GetType(Expression::NoInstance()))) {
                                        if (conty->GetConstructedType() == x.t)
                                            return &init;
                                    }
                                }
                            }
                            return nullptr;
                        };
                        auto result = analyzer.GetLvalueType(x.t);
                        // Gotta get the correct this pointer.
                        auto make_member_initializer = [&, this](std::vector<std::shared_ptr<Expression>> init, Lexer::Range where) {
                            auto member = make_member(analyzer.GetLvalueType(x.t), x.num, LookupLocal("this"));
                            auto t = x.t;
                            return CreateResultExpression(Range::Elements(member) | Range::Concat(Range::Container(init)), [=](Expression::InstanceKey key) {
                                auto construction = Type::BuildInplaceConstruction(key, member, std::move(init), { GetContext(), where });
                                auto destructor = t->IsTriviallyDestructible()
                                    ? std::function<void(CodegenContext&)>()
                                    : t->BuildDestructorCall(key, member, { GetContext(), where }, true);
                                return CreatePrimGlobal(Range::Elements(construction) | Range::Concat(Range::Container(init)), analyzer.GetVoidType(), [=](CodegenContext& con) {
                                    construction->GetValue(con);
                                    if (destructor)
                                        con.AddExceptionOnlyDestructor(destructor);
                                    return nullptr;
                                });
                            });
                        };
                        if (auto init = has_initializer()) {
                            // AccessMember will automatically give us back a T*, but we need the T** here
                            // if the type of this member is a reference.
                            used_initializers.insert(init);
                            if (init->initializer) {
                                // If it's a tuple, pass each subexpression.
                                std::vector<std::shared_ptr<Expression>> exprs;
                                if (auto tup = dynamic_cast<const Parse::Tuple*>(init->initializer.get())) {
                                    for (auto&& expr : tup->expressions)
                                        exprs.push_back(analyzer.AnalyzeExpression(GetContext(), expr.get(), root_scope.get(), NonstaticLookup));
                                } else
                                    exprs.push_back(analyzer.AnalyzeExpression(GetContext(), init->initializer.get(), root_scope.get(), NonstaticLookup));
                                root_scope->active.push_back(make_member_initializer(std::move(exprs), init->where));
                            } else
                                root_scope->active.push_back(make_member_initializer({}, init->where));
                            continue;
                        }
                        // Don't care about if x.t is ref because refs can't be default-constructed anyway.
                        if (x.InClassInitializer) {
                            root_scope->active.push_back(make_member_initializer({ x.InClassInitializer(LookupLocal("this")) }, x.location));
                            continue;
                        }
                        root_scope->active.push_back(make_member_initializer({}, fun->where));
                    }
                    for (auto&& x : con->initializers) {
                        if (used_initializers.find(&x) == used_initializers.end()) {
                            if (auto ident = dynamic_cast<const Parse::Identifier*>(x.initialized.get()))
                                throw NoMemberToInitialize(context->Decay(), ident->val, x.where);
                            auto expr = analyzer.AnalyzeExpression(GetContext(), x.initializer.get(), root_scope.get(), NonstaticLookup);
                            auto conty = dynamic_cast<ConstructorType*>(expr->GetType(Expression::NoInstance()));
                            throw NoMemberToInitialize(context->Decay(), conty->GetConstructedType()->explain(), x.where);
                        }
                    }
                    root_scope->active.push_back(SetVirtualPointers());
                }
            }
            // set the vptrs if necessary
        }

        // Check for defaulted operator=
        if (auto func = dynamic_cast<const Parse::Function*>(fun)) {
            auto BuildBinaryExpression = [=](std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, Lexer::TokenType what, Context c) {
                return CreateResultExpression(Range::Elements(lhs, rhs), [=](Expression::InstanceKey key) {
                    return Type::BuildBinaryExpression(key, lhs, rhs, what, c);
                });
            };
            if (func->defaulted) {
                auto member = dynamic_cast<Semantic::ConstructorContext*>(GetContext());
                auto members = member->GetConstructionMembers();
                if (func->args.size() != 1)
                    throw std::runtime_error("Bad defaulted function.");
                auto argty = analyzer.AnalyzeExpression(GetContext(), func->args[0].type.get(), [](Parse::Name, Lexer::Range) { return nullptr; })->GetType(Expression::NoInstance());
                if (argty == analyzer.GetLvalueType(GetContext())) {
                    // Copy assignment- copy all.
                    unsigned i = 0;
                    for (auto&& x : members) {
                        root_scope->active.push_back(BuildBinaryExpression(member->PrimitiveAccessMember(LookupLocal("this"), i), member->PrimitiveAccessMember(parameters[1], i), &Lexer::TokenTypes::Assignment, { GetContext(), fun->where }));
                    }
                } else if (argty == analyzer.GetRvalueType(GetContext())) {
                    // move assignment- move all.
                    unsigned i = 0;
                    for (auto&& x : members) {
                        root_scope->active.push_back(BuildBinaryExpression(member->PrimitiveAccessMember(LookupLocal("this"), i), member->PrimitiveAccessMember(std::make_shared<RvalueCast>(parameters[1]), i), &Lexer::TokenTypes::Assignment, { GetContext(), fun->where }));
                    }
                } else
                    throw std::runtime_error("Bad defaulted function.");
            }
        }

        // Now the body.
        for (std::size_t i = 0; i < fun->statements.size(); ++i) {
            root_scope->active.push_back(AnalyzeStatement(analyzer, this, fun->statements[i].get(), GetContext(), root_scope.get(), NonstaticLookup));
        }

        // If we were a destructor, destroy.
        if (auto des = dynamic_cast<const Parse::Destructor*>(fun)) {
            if (!ConstructorContext) throw std::runtime_error("fuck");
            auto member = *ConstructorContext;
            auto members = member->GetConstructionMembers();
            for (auto rit = members.rbegin(); rit != members.rend(); ++rit) {
                auto num = rit->num;
                auto result = analyzer.GetLvalueType(rit->t);
                auto member = CreatePrimUnOp(LookupLocal("this"), result, [num, result](llvm::Value* val, CodegenContext& con) -> llvm::Value* {
                    auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
                    self = con->CreateConstGEP1_32(self, num());
                    return con->CreatePointerCast(self, result->GetLLVMType(con));
                });
                auto destructor = rit->t->BuildDestructorCall(Expression::NoInstance(), member, { GetContext(), fun->where }, true);
                root_scope->active.push_back(CreatePrimGlobal(Range::Empty(), analyzer, [=](CodegenContext& con) {
                    destructor(con);
                }));
            }
        }
    }
    return root_scope.get();
}

std::shared_ptr<Expression> FunctionSkeleton::LookupLocal(Parse::Name name) {
    Scope* current_scope = root_scope.get();
    while (!current_scope->children.empty())
        current_scope = current_scope->children.back().get();
    if (auto string = boost::get<std::string>(&name))
        return current_scope->LookupLocal(*string);
    return nullptr;
}

void FunctionSkeleton::AddDefaultHandlers(Analyzer& a) {
    AddHandler<const Parse::Return>(a.StatementHandlers, [](const Parse::Return* ret, FunctionSkeleton* skel, Analyzer& analyzer, Type* self, Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> nonstatic) {
        auto ret_expr = ret->RetExpr ? analyzer.AnalyzeExpression(self, ret->RetExpr.get(), current, nonstatic) : nullptr;
        return CreateResultExpression(Range::Elements(ret_expr), [=, &analyzer](Expression::InstanceKey key) -> std::shared_ptr<Expression> {
            if (!key) return nullptr;
            auto func = analyzer.GetWideFunction(skel, *key);
            func->AddReturnExpression(ret_expr.get());
            if (!ret_expr) return CreatePrimGlobal(Range::Empty(), analyzer, [](CodegenContext& con) {});
            std::shared_ptr<std::shared_ptr<Expression>> build = std::make_shared<std::shared_ptr<Expression>>();
            func->ReturnTypeChanged.connect([=, &analyzer](Type* t) {
                if (t != analyzer.GetVoidType() && t) {
                    *build = Type::BuildInplaceConstruction(
                        key,
                        CreatePrimGlobal(Range::Elements(ret_expr), analyzer.GetLvalueType(t), [](CodegenContext& con) { return con->GetInsertBlock()->getParent()->arg_begin(); }),
                        { ret_expr }, { self, ret->location }
                    );
                    (*build)->GetType(key);
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
                    if (ret_expr->GetType(con.func) == func->GetSignature()->GetReturnType()) {
                        // then great, just return it directly.
                        auto val = ret_expr->GetValue(con);
                        con.DestroyAll(false);
                        con->CreateRet(val);
                        return;
                    }
                    // If we have a reference to it, just load it right away.
                    if (ret_expr->GetType(con.func)->IsReference(func->GetSignature()->GetReturnType())) {
                        auto val = con->CreateLoad(ret_expr->GetValue(con));
                        con.DestroyAll(false);
                        con->CreateRet(val);
                        return;
                    }
                    // Build will inplace construct this in our first argument, which is INCREDIBLY UNHELPFUL here.
                    // We would fix this up, but, cannot query the complexity of a type prior to code generation.
                    auto val = func->GetSignature()->GetReturnType()->BuildValueConstruction(con.func, { ret_expr }, { self, ret->RetExpr->location })->GetValue(con);
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

    AddHandler<const Parse::CompoundStatement>(a.StatementHandlers, [](const Parse::CompoundStatement* comp, FunctionSkeleton* skel, Analyzer& analyzer, Type* self, Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> nonstatic) {
        auto compound = new Scope(current);
        for (auto&& stmt : comp->stmts)
            compound->active.push_back(AnalyzeStatement(analyzer, skel, stmt.get(), self, compound, nonstatic));
        return CreatePrimGlobal(Range::Empty(), analyzer, [=](CodegenContext& con) {
            con.GenerateCodeAndDestroyLocals([=](CodegenContext& con) {
                for (auto&& stmt : compound->active)
                    if (!con.IsTerminated(con->GetInsertBlock()))
                        stmt->GenerateCode(con);
            });
        });
    });

    AddHandler<const Parse::Variable>(a.StatementHandlers, [](const Parse::Variable* var, FunctionSkeleton* skel, Analyzer& analyzer, Type* self, Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> nonstatic) {
        auto init_expr = analyzer.AnalyzeExpression(self, var->initializer.get(), current, nonstatic);
        auto var_type_expr = var->type ? analyzer.AnalyzeExpression(self, var->type.get(), current, nonstatic) : nullptr;
        if (var->name.size() == 1) {
            auto&& name = var->name.front();
            if (current->named_variables.find(var->name.front().name) != current->named_variables.end())
                throw VariableShadowing(name.name, current->named_variables.at(name.name).second, name.where);
            auto var = CreateResultExpression(Range::Elements(init_expr, var_type_expr), [=, &analyzer](Expression::InstanceKey key) {
                Type* var_type = nullptr;
                if (var_type_expr) {
                    auto conty = dynamic_cast<ConstructorType*>(var_type_expr->GetType(key)->Decay());
                    if (!conty) throw std::runtime_error("Local variable type was not a type.");
                    var_type = conty->GetConstructedType();
                } else
                    var_type = init_expr->GetType(key)->Decay();
                if (var_type == init_expr->GetType(key) && var_type->IsReference())
                    return CreatePrimGlobal(Range::Elements(init_expr), var_type, [=](CodegenContext& con) -> llvm::Value* {
                        return init_expr->GetValue(con);
                    });
                if (var_type == init_expr->GetType(key))
                    return CreatePrimGlobal(Range::Elements(init_expr), analyzer.GetLvalueType(var_type), [=](CodegenContext& con) -> llvm::Value* {
                        if (init_expr->GetType(con.func)->AlwaysKeepInMemory(con)) return init_expr->GetValue(con);
                        auto alloc = con.CreateAlloca(var_type);
                        con->CreateStore(init_expr->GetValue(con), alloc);
                        return alloc;
                    });
                auto temp = CreateTemporary(var_type, { self, name.where });
                auto construct = Type::BuildInplaceConstruction(key, temp, { init_expr }, { self, name.where });
                auto des = var_type->BuildDestructorCall(key, temp, { self, name.where }, true);
                return CreatePrimGlobal(Range::Elements(construct), analyzer.GetLvalueType(var_type), [=](CodegenContext& con) {
                    construct->GetValue(con);
                    con.AddDestructor(des);
                    return temp->GetValue(con);
                });
            });
            current->named_variables.insert(std::make_pair(name.name, std::make_pair(var, name.where)));
            return var;
        }
        std::vector<std::shared_ptr<Expression>> exprs;
        for (auto&& name : var->name) {
            auto offset = &name - &var->name[0];
            if (current->named_variables.find(name.name) != current->named_variables.end())
                throw VariableShadowing(name.name, current->named_variables.at(name.name).second, name.where);
            auto local = CreateResultExpression(Range::Elements(init_expr, var_type_expr), [=, &analyzer](Expression::InstanceKey key) {
                Type* var_type = nullptr;
                std::shared_ptr<Expression> local_init;
                if (var_type_expr) {
                    auto conty = dynamic_cast<ConstructorType*>(var_type_expr->GetType(key)->Decay());
                    if (!conty) throw std::runtime_error("Local variable type was not a type.");
                    auto tup = dynamic_cast<TupleType*>(conty->GetConstructedType());
                    if (!tup || tup->GetMembers().size() != var->name.size())
                        throw std::runtime_error("Tuple size and name number mismatch.");
                    var_type = tup->GetMembers()[offset];
                    local_init = tup->PrimitiveAccessMember(init_expr, offset);
                } else {
                    auto tup = dynamic_cast<TupleType*>(init_expr->GetType(key)->Decay());
                    if (!tup || tup->GetMembers().size() != var->name.size())
                        throw std::runtime_error("Tuple size and name number mismatch.");
                    var_type = tup->GetMembers()[offset]->Decay();
                    local_init = tup->PrimitiveAccessMember(init_expr, offset);
                }
                auto temp = CreateTemporary(var_type, { self, name.where });
                auto construct = Type::BuildInplaceConstruction(key, temp, { local_init }, { self, name.where });
                auto des = var_type->BuildDestructorCall(key, temp, { self, name.where }, true);
                return CreatePrimGlobal(Range::Elements(construct), analyzer.GetLvalueType(var_type), [=](CodegenContext& con) {
                    construct->GetValue(con);
                    con.AddDestructor(des);
                    return temp->GetValue(con);
                });
            });
            current->named_variables.insert(std::make_pair(name.name, std::make_pair(local, name.where)));
            exprs.push_back(local);
        }
        return CreateResultExpression(Range::Elements(init_expr, var_type_expr) | Range::Concat(Range::Container(exprs)), [=, &analyzer](Expression::InstanceKey key) {
            return CreatePrimGlobal(Range::Container(exprs), analyzer, [=](CodegenContext& con) {
                for (auto&& var : exprs)
                    var->GetValue(con);
            });
        });
    });

    AddHandler<const Parse::While>(a.StatementHandlers, [](const Parse::While* whil, FunctionSkeleton* skel, Analyzer& analyzer, Type* self, Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> nonstatic) -> std::shared_ptr<Statement> {
        struct WhileStatement : Statement, ControlFlowStatement {
            Type* self;
            Lexer::Range where;
            std::shared_ptr<Statement> body;
            std::shared_ptr<Expression> boolconvert;
            llvm::BasicBlock* continue_bb = nullptr;
            llvm::BasicBlock* check_bb = nullptr;
            CodegenContext* source_con = nullptr;
            CodegenContext* condition_con = nullptr;

            WhileStatement(std::shared_ptr<Expression> ex, Lexer::Range where, Type* s)
                : where(where), self(s)
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
            void Instantiate(Function* f) override final {
                body->Instantiate(f);
                boolconvert->GetType(f->GetArguments());
            }
        };
        auto condscope = new Scope(current);
        auto get_expr = [&, self]() -> std::shared_ptr<Expression> {
            if (whil->var_condition) {
                if (whil->var_condition->name.size() != 1)
                    throw std::runtime_error("fuck");
                condscope->active.push_back(AnalyzeStatement(analyzer, skel, whil->var_condition.get(), self, condscope, nonstatic));
                return condscope->named_variables.begin()->second.first;
            }
            return analyzer.AnalyzeExpression(self, whil->condition.get(), current, nonstatic);
        };
        auto ex = get_expr();
        auto cond = CreateResultExpression(Range::Elements(ex), [=](Expression::InstanceKey key) {
            return Type::BuildBooleanConversion(key, ex, Context(self, whil->location));
        });
        auto while_stmt = std::make_shared<WhileStatement>(cond, whil->location, self);
        condscope->active.push_back(std::move(cond));
        condscope->control_flow = while_stmt.get();
        auto bodyscope = new Scope(condscope);
        bodyscope->active.push_back(AnalyzeStatement(analyzer, skel, whil->body.get(), self, bodyscope, nonstatic));
        while_stmt->body = bodyscope->active.back();
        return while_stmt;
    });

    AddHandler<const Parse::Continue>(a.StatementHandlers, [](const Parse::Continue* continue_stmt, FunctionSkeleton* skel, Analyzer& analyzer, Type* self, Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> nonstatic) {
        auto flow = current->GetCurrentControlFlow();
        if (!flow)
            throw NoControlFlowStatement(continue_stmt->location);
        return CreatePrimGlobal(Range::Empty(), analyzer, [=](CodegenContext& con) {
            flow->JumpForContinue(con);
        });
    });

    AddHandler<const Parse::Break>(a.StatementHandlers, [](const Parse::Break* break_stmt, FunctionSkeleton* skel, Analyzer& analyzer, Type* self, Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> nonstatic) {
        auto flow = current->GetCurrentControlFlow();
        if (!flow)
            throw NoControlFlowStatement(break_stmt->location);
        return CreatePrimGlobal(Range::Empty(), analyzer, [=](CodegenContext& con) {
            flow->JumpForBreak(con);
        });
    });

    AddHandler<const Parse::If>(a.StatementHandlers, [](const Parse::If* if_stmt, FunctionSkeleton* skel, Analyzer& analyzer, Type* self, Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> nonstatic) {
        auto condscope = new Scope(current);
        auto get_expr = [&, self]() -> std::shared_ptr<Expression> {
            if (if_stmt->var_condition) {
                if (if_stmt->var_condition->name.size() != 1)
                    throw std::runtime_error("fuck");
                condscope->active.push_back(AnalyzeStatement(analyzer, skel, if_stmt->var_condition.get(), self, condscope, nonstatic));
                return condscope->named_variables.begin()->second.first;
            }
            return analyzer.AnalyzeExpression(self, if_stmt->condition.get(), current, nonstatic);
        };
        auto cond_expr = get_expr();
        auto cond = CreateResultExpression(Range::Elements(cond_expr), [=](Expression::InstanceKey key) {
            return Type::BuildBooleanConversion(key, cond_expr, { self, if_stmt->location });
        });
        condscope->active.push_back(cond);
        Statement* true_br = nullptr;
        {
            auto truescope = new Scope(condscope);
            truescope->active.push_back(AnalyzeStatement(analyzer, skel, if_stmt->true_statement.get(), self, truescope, nonstatic));
            true_br = truescope->active.back().get();
        }
        Statement* false_br = nullptr;
        if (if_stmt->false_statement) {
            auto falsescope = new Scope(condscope);
            falsescope->active.push_back(AnalyzeStatement(analyzer, skel, if_stmt->false_statement.get(), self, falsescope, nonstatic));
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

    AddHandler<const Parse::Throw>(a.StatementHandlers, [](const Parse::Throw* thro, FunctionSkeleton* skel, Analyzer& analyzer, Type* self, Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> nonstatic) {
        if (!thro->expr) return CreatePrimGlobal(Range::Empty(), analyzer, [=](CodegenContext& con) {
            if (con.HasDestructors() || con.EHHandler)
                con->CreateInvoke(con.GetCXARethrow(), con.GetUnreachableBlock(), con.CreateLandingpadForEH());
            else
                con->CreateCall(con.GetCXARethrow());
        });
        auto expr = analyzer.AnalyzeExpression(self, thro->expr.get(), current, nonstatic);
        return CreateResultExpression(Range::Elements(expr), [=, &analyzer](Expression::InstanceKey key) {
            return CreatePrimGlobal(Range::Empty(), analyzer, Semantic::ThrowObject(key, expr, { self, thro->location }));
        });        
    });

    AddHandler<const Parse::TryCatch>(a.StatementHandlers, [](const Parse::TryCatch* try_, FunctionSkeleton* skel, Analyzer& analyzer, Type* self, Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> nonstatic) {
        struct Catch {
            Scope* cscope;
            std::shared_ptr<llvm::Value*> val;
            std::shared_ptr<Expression> type;
            std::function<llvm::Constant*(llvm::Module*)> RTTI;
        };
        auto tryscope = new Scope(current);
        for (auto&& stmt : try_->statements->stmts)
            tryscope->active.push_back(AnalyzeStatement(analyzer, skel, stmt.get(), self, tryscope, nonstatic));
        std::vector<Catch> catches;
        for (auto&& catch_ : try_->catches) {
            auto catchscope = new Scope(current);
            if (catch_.all) {
                std::vector<std::shared_ptr<Statement>> stmts;
                for (auto&& stmt : catch_.statements)
                    stmts.push_back(AnalyzeStatement(analyzer, skel, stmt.get(), self, catchscope, nonstatic));
                catchscope->active = std::move(stmts);
                catches.push_back(Catch{ catchscope });
                break;
            }
            auto type = analyzer.AnalyzeExpression(self, catch_.type.get(), catchscope, nonstatic);
            auto param = std::make_shared<llvm::Value*>();
            auto catch_param = CreateResultExpression(Range::Elements(type), [=](Expression::InstanceKey key) {
                auto con = dynamic_cast<ConstructorType*>(type->GetType(key));
                if (!con) throw std::runtime_error("Catch parameter type was not a type.");
                auto catch_type = con->GetConstructedType();
                if (!IsLvalueType(catch_type) && !dynamic_cast<PointerType*>(catch_type))
                    throw std::runtime_error("Attempted to catch by non-lvalue and nonpointer.");
                auto target_type = IsLvalueType(catch_type)
                    ? catch_type->Decay()
                    : dynamic_cast<PointerType*>(catch_type)->GetPointee();
                return CreatePrimGlobal(Range::Elements(type), target_type, [=](CodegenContext& con) {
                    return con->CreatePointerCast(*param, target_type->GetLLVMType(con));
                });
            });
            catchscope->named_variables.insert(std::make_pair(catch_.name, std::make_pair(catch_param, catch_.type->location)));
            std::vector<std::shared_ptr<Statement>> stmts;
            for (auto&& stmt : catch_.statements)
                stmts.push_back(AnalyzeStatement(analyzer, skel, stmt.get(), self, catchscope, nonstatic));
            catchscope->active = std::move(stmts);
            catches.push_back(Catch{ catchscope, param, type });
        }
        return CreateResultExpression(Range::Container(catches) | Range::Map([](Catch cat) { return cat.type; }), [=, &analyzer](Expression::InstanceKey key) {
            auto catch_blocks = catches;
            for (auto&& catch_ : catch_blocks) {
                if (!catch_.type) continue;
                auto con = dynamic_cast<ConstructorType*>(catch_.type->GetType(key));
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

                for (auto&& stmt : tryscope->active)
                    if (!try_con.IsTerminated(try_con->GetInsertBlock()))
                        stmt->GenerateCode(try_con);
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
                    CodegenContext catch_block_con(catch_con);
                    if (!catch_.val) {
                        for (auto&& stmt : catch_.cscope->active)
                            if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock()))
                                stmt->GenerateCode(catch_block_con);
                        if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock())) {
                            con.DestroyDifference(catch_block_con, false);
                            catch_block_con->CreateBr(dest_block);
                        }
                        break;
                    }
                    auto catch_target = llvm::BasicBlock::Create(con, "catch_target", catch_block_con->GetInsertBlock()->getParent());
                    auto catch_continue = llvm::BasicBlock::Create(con, "catch_continue", catch_block_con->GetInsertBlock()->getParent());
                    auto target_selector = catch_block_con->CreateCall(for_, { con->CreatePointerCast(catch_.RTTI(con), con.GetInt8PtrTy()) });
                    auto result = catch_block_con->CreateICmpEQ(selector, target_selector);
                    catch_block_con->CreateCondBr(result, catch_target, catch_continue);
                    catch_block_con->SetInsertPoint(catch_target);
                    // Call __cxa_begin_catch and get our result. We don't need __cxa_get_exception_ptr as Wide cannot catch by value.
                    *catch_.val = catch_block_con->CreateCall(catch_block_con.GetCXABeginCatch(), { except_object });
                    // Ensure __cxa_end_catch is called.
                    catch_block_con.AddDestructor(catch_ender);

                    for (auto&& stmt : catch_.cscope->active)
                        if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock()))
                            stmt->GenerateCode(catch_block_con);
                    if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock())) {
                        con.DestroyDifference(catch_block_con, false);
                        catch_block_con->CreateBr(dest_block);
                    }
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