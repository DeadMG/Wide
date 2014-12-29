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

void FunctionSkeleton::AddExportName(std::function<void(llvm::Module*)> func) {
    trampoline.push_back(func);
}
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

FunctionSkeleton::FunctionSkeleton(std::vector<Type*> args, const Parse::FunctionBase* astfun, Analyzer& a, Type* mem, std::string src_name, Type* nonstatic_context)
    : MetaType(a)
    , ReturnType(nullptr)
    , fun(astfun)
    , context(mem)
    , s(State::NotYetAnalyzed)
    , root_scope(nullptr)
    , source_name(src_name) {
    llvmname = a.GetUniqueFunctionName();
    assert(mem);
    // Only match the non-concrete arguments.
    root_scope = Wide::Memory::MakeUnique<Scope>(nullptr);
    unsigned num = 0;
    Args = args;
    // We might still be a member function if we're exported as one later.                
    struct Parameter : Expression, std::enable_shared_from_this<Parameter> {
        Lexer::Range where;
        Function* self;
        unsigned num;
        std::function<void(CodegenContext&)> destructor;
        Type* cur_ty;

        Parameter(Function* s, unsigned n, Lexer::Range where)
            : self(s), num(n), where(where), cur_ty(nullptr)
        {
            ListenToNode(self);
        }
        void OnNodeChanged(Node* n, Change what) override final {
            auto get_new_ty = [this]() -> Type* {
                auto root_ty = self->Args[num];
                if (root_ty->IsReference())
                    return self->analyzer.GetLvalueType(root_ty->Decay()); // Is this wrong in the case of named rvalue reference?
                return self->analyzer.GetLvalueType(root_ty);
            };
            auto new_ty = get_new_ty();
            if (new_ty != cur_ty) {
                cur_ty = new_ty;
                OnChange();
            }
        }
        Type* GetType() override final  {
            return cur_ty;
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            auto argnum = num;
            if (self->ReturnType->AlwaysKeepInMemory(con))
                ++argnum;
            auto llvm_argument = std::next(self->llvmfunc->arg_begin(), argnum);

            if (self->Args[num]->AlwaysKeepInMemory(con) || self->Args[num]->IsReference())
                return llvm_argument;

            auto alloc = con.CreateAlloca(self->Args[num]);
            con->CreateStore(llvm_argument, alloc);
            return alloc;
        }
    };
    if (nonstatic_context) {
        NonstaticMemberContext = nonstatic_context;
        if (auto con = dynamic_cast<Semantic::ConstructorContext*>(nonstatic_context))
            ConstructorContext = con;
        auto param = std::make_shared<Parameter>(this, num++, Lexer::Range(nullptr));
        root_scope->named_variables.insert(std::make_pair("this", std::make_pair(param, Lexer::Range(nullptr))));
        root_scope->active.push_back(param);
        parameters.push_back(param);
        param->OnNodeChanged(nullptr, Change::Contents);
    }
    for (auto&& arg : astfun->args) {
        if (arg.name == "this")
            continue;
        auto param = std::make_shared<Parameter>(this, num++, arg.location);
        root_scope->named_variables.insert(std::make_pair(arg.name, std::make_pair(param, arg.location)));
        root_scope->active.push_back(param);
        parameters.push_back(param);
        param->OnNodeChanged(nullptr, Change::Contents);
    }

    // Deal with the exports first, if any
    if (auto fun = dynamic_cast<const Parse::AttributeFunctionBase*>(astfun)) {
        for (auto&& attr : fun->attributes) {
            if (auto name = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
                if (auto string = boost::get<std::string>(&name->val)) {
                    if (*string == "export") {
                        auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer.get());
                        auto overset = dynamic_cast<OverloadSet*>(expr->GetType()->Decay());
                        if (!overset)
                            continue;
                        //throw NotAType(expr->GetType()->Decay(), attr.initializer->location);
                        auto tuanddecl = overset->GetSingleFunction();
                        if (!tuanddecl.second) throw NotAType(expr->GetType()->Decay(), attr.initializer->location);
                        auto tu = tuanddecl.first;
                        auto decl = tuanddecl.second;
                        std::function<llvm::Function*(llvm::Module*)> source;

                        if (auto des = llvm::dyn_cast<clang::CXXDestructorDecl>(decl))
                            source = tu->GetObject(analyzer, des, clang::CXXDtorType::Dtor_Complete);
                        else if (auto con = llvm::dyn_cast<clang::CXXConstructorDecl>(decl))
                            source = tu->GetObject(analyzer, con, clang::CXXCtorType::Ctor_Complete);
                        else
                            source = tu->GetObject(analyzer, decl);
                        clang_exports.push_back(std::make_tuple(source, GetFunctionType(decl, *tu, analyzer), decl));
                    }
                    if (*string == "import_name") {
                        auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer.get());
                        auto string = dynamic_cast<String*>(expr.get());
                        import_name = string->str;
                    }
                }
            }
        }
    }

    // Explicit return type, if any
    if (auto fun = dynamic_cast<const Parse::Function*>(astfun)) {
        if (fun->explicit_return) {
            auto expr = analyzer.AnalyzeExpression(this, fun->explicit_return.get());
            if (auto con = dynamic_cast<ConstructorType*>(expr->GetType()->Decay())) {
                ExplicitReturnType = con->GetConstructedType();
                ReturnType = *ExplicitReturnType;
            } else
                throw NotAType(expr->GetType(), fun->explicit_return->location);
        }
    }
    // Constructors and destructors, we know in advance to return void.
    if (dynamic_cast<const Parse::Constructor*>(fun) || dynamic_cast<const Parse::Destructor*>(fun)) {
        ExplicitReturnType = analyzer.GetVoidType();
        ReturnType = *ExplicitReturnType;
    }
}

Wide::Util::optional<clang::QualType> FunctionSkeleton::GetClangType(ClangTU& where) {
    return GetSignature()->GetClangType(where);
}

void FunctionSkeleton::ComputeBody() {
    if (s == State::NotYetAnalyzed) {
        s = State::AnalyzeInProgress;
        // If we're imported, skip everything.
        if (import_name) {
            ComputeReturnType(); // Must have explicit return type for now.
            s = State::AnalyzeCompleted;
            return;
        }
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
            // Are we defaulted?
            if (con->defaulted) {
                // Only accept some arguments.
                if (Args.size() == 1) {
                    // Default-or-NSDMI all the things.
                    for (auto&& x : members) {
                        auto member = make_member(analyzer.GetLvalueType(x.t), x.num, LookupLocal("this"));
                        std::vector<std::shared_ptr<Expression>> inits;
                        if (x.InClassInitializer)
                            inits = { x.InClassInitializer(LookupLocal("this")) };
                        root_scope->active.push_back(Type::BuildInplaceConstruction(member, inits, { this, con->where }));
                    }
                    root_scope->active.push_back(Type::SetVirtualPointers(LookupLocal("this")));
                } else if (Args[1] == analyzer.GetLvalueType(GetContext())) {
                    // Copy constructor- copy all.
                    unsigned i = 0;
                    for (auto&& x : members) {
                        auto member_ref = make_member(analyzer.GetLvalueType(x.t), x.num, LookupLocal("this"));
                        root_scope->active.push_back(Type::BuildInplaceConstruction(member_ref, { member->PrimitiveAccessMember(parameters[1], i++) }, { this, con->where }));
                    }
                    root_scope->active.push_back(Type::SetVirtualPointers(LookupLocal("this")));
                } else if (Args[1] == analyzer.GetRvalueType(GetContext())) {
                    // move constructor- move all.
                    unsigned i = 0;
                    for (auto&& x : members) {
                        auto member_ref = make_member(analyzer.GetLvalueType(x.t), x.num, LookupLocal("this"));
                        root_scope->active.push_back(Type::BuildInplaceConstruction(member_ref, { member->PrimitiveAccessMember(std::make_shared<RvalueCast>(parameters[1]), i++) }, { this, con->where }));
                    }
                    root_scope->active.push_back(Type::SetVirtualPointers(LookupLocal("this")));
                } else
                    throw std::runtime_error("Fuck.");
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
                    Context c{ this, con->initializers.front().where };
                    auto expr = analyzer.AnalyzeExpression(this, con->initializers.front().initializer.get());
                    auto conty = dynamic_cast<Type*>(*ConstructorContext);
                    auto conoverset = conty->GetConstructorOverloadSet(Parse::Access::Private);
                    auto callable = conoverset->Resolve({ LookupLocal("this")->GetType(), expr->GetType() }, this);
                    if (!callable)
                        throw std::runtime_error("Couldn't resolve delegating constructor call.");
                    root_scope->active.push_back(callable->Call({ LookupLocal("this"), expr }, c));
                    struct Destructor : Expression {
                        Destructor(std::function<void(CodegenContext&)> destructor, Type* void_ty)
                            : destructor(destructor), void_ty(void_ty) {}
                        std::function<void(CodegenContext&)> destructor;
                        Type* void_ty;
                        Type* GetType() override final { return void_ty; }
                        llvm::Value* ComputeValue(CodegenContext& con) {
                            con.AddExceptionOnlyDestructor(destructor);
                            return nullptr;
                        }
                    };
                    if (!conty->IsTriviallyDestructible())
                        root_scope->active.push_back(std::make_shared<Destructor>(conty->BuildDestructorCall(LookupLocal("this"), c, true), analyzer.GetVoidType()));
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
                                    auto ty = analyzer.AnalyzeExpression(this, init.initialized.get());
                                    if (auto conty = dynamic_cast<ConstructorType*>(ty->GetType())) {
                                        if (conty->GetConstructedType() == x.t)
                                            return &init;
                                    }
                                }
                            }
                            return nullptr;
                        };
                        auto result = analyzer.GetLvalueType(x.t);
                        // Gotta get the correct this pointer.
                        struct MemberConstructionAccess : Expression {
                            Type* member;
                            Lexer::Range where;
                            std::shared_ptr<Expression> Construction;
                            std::shared_ptr<Expression> memexpr;
                            std::function<void(CodegenContext&)> destructor;
                            llvm::Value* ComputeValue(CodegenContext& con) override final {
                                auto val = Construction->GetValue(con);
                                if (destructor)
                                    con.AddExceptionOnlyDestructor(destructor);
                                return val;
                            }
                            Type* GetType() override final {
                                return Construction->GetType();
                            }
                            MemberConstructionAccess(Type* mem, Lexer::Range where, std::shared_ptr<Expression> expr, std::shared_ptr<Expression> memexpr)
                                : member(mem), where(where), Construction(std::move(expr)), memexpr(memexpr)
                            {
                                if (!member->IsTriviallyDestructible())
                                    destructor = member->BuildDestructorCall(memexpr, { member, where }, true);
                            }
                        };
                        auto make_member_initializer = [&, this](std::vector<std::shared_ptr<Expression>> init, Lexer::Range where) {
                            auto member = make_member(analyzer.GetLvalueType(x.t), x.num, LookupLocal("this"));
                            auto construction = Type::BuildInplaceConstruction(member, std::move(init), { this, where });
                            return Wide::Memory::MakeUnique<MemberConstructionAccess>(x.t, where, std::move(construction), member);
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
                                        exprs.push_back(analyzer.AnalyzeExpression(this, expr.get()));
                                } else
                                    exprs.push_back(analyzer.AnalyzeExpression(this, init->initializer.get()));
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
                            auto expr = analyzer.AnalyzeExpression(this, x.initializer.get());
                            auto conty = dynamic_cast<ConstructorType*>(expr->GetType());
                            throw NoMemberToInitialize(context->Decay(), conty->GetConstructedType()->explain(), x.where);
                        }
                    }
                    root_scope->active.push_back(Type::SetVirtualPointers(LookupLocal("this")));
                }
            }
            // set the vptrs if necessary
        }

        // Check for defaulted operator=
        if (auto func = dynamic_cast<const Parse::Function*>(fun)) {
            if (func->defaulted) {
                auto member = dynamic_cast<Semantic::ConstructorContext*>(GetContext());
                auto members = member->GetConstructionMembers();
                if (Args.size() != 2)
                    throw std::runtime_error("Bad defaulted function.");
                if (Args[1] == analyzer.GetLvalueType(GetContext())) {
                    // Copy constructor- copy all.
                    unsigned i = 0;
                    for (auto&& x : members) {
                        root_scope->active.push_back(Type::BuildBinaryExpression(member->PrimitiveAccessMember(LookupLocal("this"), i), member->PrimitiveAccessMember(parameters[1], i), &Lexer::TokenTypes::Assignment, { this, fun->where }));
                    }
                } else if (Args[1] == analyzer.GetRvalueType(GetContext())) {
                    // move constructor- move all.
                    unsigned i = 0;
                    for (auto&& x : members) {
                        root_scope->active.push_back(Type::BuildBinaryExpression(member->PrimitiveAccessMember(LookupLocal("this"), i), member->PrimitiveAccessMember(std::make_shared<RvalueCast>(parameters[1]), i), &Lexer::TokenTypes::Assignment, { this, fun->where }));
                    }
                } else
                    throw std::runtime_error("Bad defaulted function.");
            }
        }

        // Now the body.
        for (std::size_t i = 0; i < fun->statements.size(); ++i) {
            root_scope->active.push_back(AnalyzeStatement(analyzer, fun->statements[i].get(), this, root_scope.get(), [this](Return* r) {
                returns.insert(r);
                return std::function<Type*()>([this] { return ReturnType; });
            }));
        }

        // If we were a destructor, destroy.
        if (auto des = dynamic_cast<const Parse::Destructor*>(fun)) {
            if (!ConstructorContext) throw std::runtime_error("fuck");
            auto member = *ConstructorContext;
            auto members = member->GetConstructionMembers();
            for (auto rit = members.rbegin(); rit != members.rend(); ++rit) {
                struct DestructorCall : Expression {
                    DestructorCall(std::function<void(CodegenContext&)> destructor, Analyzer& a)
                        : destructor(destructor), a(&a) {}
                    std::function<void(CodegenContext&)> destructor;
                    Analyzer* a;
                    Type* GetType() override final {
                        return a->GetVoidType();
                    }
                    llvm::Value* ComputeValue(CodegenContext& con) override final {
                        destructor(con);
                        return nullptr;
                    }
                };
                auto num = rit->num;
                auto result = analyzer.GetLvalueType(rit->t);
                auto member = CreatePrimUnOp(LookupLocal("this"), result, [num, result](llvm::Value* val, CodegenContext& con) -> llvm::Value* {
                    auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
                    self = con->CreateConstGEP1_32(self, num());
                    return con->CreatePointerCast(self, result->GetLLVMType(con));
                });
                root_scope->active.push_back(std::make_shared<DestructorCall>(rit->t->BuildDestructorCall(member, { this, fun->where }, true), analyzer));
            }
        }

        // Compute the return type.
        ComputeReturnType();
        s = State::AnalyzeCompleted;
        for (auto pair : clang_exports) {
            if (!FunctionType::CanThunkFromFirstToSecond(std::get<1>(pair), GetSignature(), this, false))
                throw std::runtime_error("Tried to export to a function decl, but the signatures were incompatible.");
            trampoline.push_back(std::get<1>(pair)->CreateThunk(std::get<0>(pair), GetStaticSelf(), std::get<2>(pair), this));
        }
    }
}

void FunctionSkeleton::ComputeReturnType() {
    if (!ExplicitReturnType) {
        if (returns.size() == 0) {
            ReturnType = analyzer.GetVoidType();
            return;
        }

        std::unordered_set<Type*> ret_types;
        for (auto ret : returns) {
            if (!ret->GetReturnType()) continue;
            ret_types.insert(ret->GetReturnType()->Decay());
        }

        if (ret_types.size() == 1) {
            ReturnType = *ret_types.begin();
            OnChange();
            return;
        }

        // If there are multiple return types, there should be a single return type where the rest all is-a that one.
        std::unordered_set<Type*> isa_rets;
        for (auto ret : ret_types) {
            auto the_rest = ret_types;
            the_rest.erase(ret);
            auto all_isa = [&] {
                for (auto other : the_rest) {
                    if (!Type::IsFirstASecond(other, ret, this))
                        return false;
                }
                return true;
            };
            if (all_isa())
                isa_rets.insert(ret);
        }
        if (isa_rets.size() == 1) {
            ReturnType = *isa_rets.begin();
            OnChange();
            return;
        }
        throw std::runtime_error("Fuck");
    } else {
        ReturnType = *ExplicitReturnType;
    }
}

llvm::Function* FunctionSkeleton::EmitCode(llvm::Module* module) {
    if (llvmfunc) {
        if (llvmfunc->getParent() == module)
            return llvmfunc;
        return module->getFunction(llvmfunc->getName());
    }
    auto sig = GetSignature();
    auto llvmsig = sig->GetLLVMType(module);
    if (import_name) {
        if (llvmfunc = module->getFunction(*import_name))
            return llvmfunc;
        llvmfunc = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(llvmsig->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, *import_name, module);
        for (auto exportnam : trampoline)
            exportnam(module);
        return llvmfunc;
    }
    llvmfunc = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(llvmsig->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, llvmname, module);
    CodegenContext::EmitFunctionBody(llvmfunc, [this](CodegenContext& c) {
        for (auto&& stmt : root_scope->active)
            if (!c.IsTerminated(c->GetInsertBlock()))
                stmt->GenerateCode(c);

        if (!c.IsTerminated(c->GetInsertBlock())) {
            if (ReturnType == analyzer.GetVoidType()) {
                c.DestroyAll(false);
                c->CreateRetVoid();
            } else
                c->CreateUnreachable();
        }
    });

    for (auto exportnam : trampoline)
        exportnam(module);
    return llvmfunc;
}

std::shared_ptr<Expression> FunctionSkeleton::ConstructCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    if (s == State::NotYetAnalyzed)
        ComputeBody();
    struct Self : public Expression {
        Self(Function* self, std::shared_ptr<Expression> expr, std::shared_ptr<Expression> val)
            : self(self), val(std::move(val))
        {
            if (!expr) return;
            if (auto func = dynamic_cast<const Parse::DynamicFunction*>(self->fun)) {
                udt = dynamic_cast<UserDefinedType*>(expr->GetType()->Decay());
                if (!udt)
                    return;
                obj = Type::GetVirtualPointer(expr);
            }
        }
        UserDefinedType* udt;
        std::shared_ptr<Expression> obj;
        std::shared_ptr<Expression> val;
        Function* self;
        Type* GetType() override final {
            return self->GetSignature();
        }
        bool IsConstantExpression() override final {
            if (obj) return obj->IsConstantExpression() && val->IsConstantExpression();
            return val->IsConstantExpression();
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            if (!self->llvmfunc)
                self->EmitCode(con);
            val->GetValue(con);
            if (obj) {
                auto func = dynamic_cast<const Parse::DynamicFunction*>(self->fun);
                assert(func);
                auto vindex = udt->GetVirtualFunctionIndex(func);
                if (!vindex) return self->llvmfunc;
                auto vptr = con->CreateLoad(obj->GetValue(con));
                return con->CreatePointerCast(con->CreateLoad(con->CreateConstGEP1_32(vptr, *vindex)), self->GetSignature()->GetLLVMType(con));
            }
            return self->llvmfunc;
        }
    };
    auto self = !args.empty() ? args[0] : nullptr;

    return Type::BuildCall(Wide::Memory::MakeUnique<Self>(this, self, std::move(val)), std::move(args), c);
}

std::shared_ptr<Expression> FunctionSkeleton::LookupLocal(Parse::Name name) {
    Scope* current_scope = root_scope.get();
    while (!current_scope->children.empty())
        current_scope = current_scope->children.back().get();
    if (auto string = boost::get<std::string>(&name))
        return current_scope->LookupLocal(*string);
    return nullptr;
}
WideFunctionType* FunctionSkeleton::GetSignature() {
    if (s == State::NotYetAnalyzed)
        ComputeBody();
    if (ExplicitReturnType)
        return analyzer.GetFunctionType(ReturnType, Args, false);
    if (s == State::AnalyzeInProgress)
        assert(false && "Attempted to call GetSignature whilst a function was still being analyzed.");
    assert(ReturnType);
    return analyzer.GetFunctionType(ReturnType, Args, false);
}

Type* FunctionSkeleton::GetConstantContext() {
    return nullptr;
}
std::string FunctionSkeleton::explain() {
    auto args = std::string("(");
    unsigned i = 0;
    for (auto& ty : Args) {
        if (Args.size() == fun->args.size() + 1 && i == 0) {
            args += "this := ";
            ++i;
        } else {
            args += fun->args[i++].name + " := ";
        }
        if (&ty != &Args.back())
            args += ty->explain() + ", ";
        else
            args += ty->explain();
    }
    args += ")";

    std::string context_name = context->explain() + "." + source_name;
    if (context == analyzer.GetGlobalModule())
        context_name = "." + source_name;
    return context_name + args + " at " + fun->where;
}
Function::~Function() {}
std::vector<std::shared_ptr<Expression>> FunctionSkeleton::AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) {
    // May need to perform conversion on "this" that isn't handled by the usual machinery.
    // But check first, because e.g. Derived& to Base& is fine.
    if (analyzer.HasImplicitThis(fun, context) && !Type::IsFirstASecond(args[0]->GetType(), Args[0], context)) {
        auto argty = args[0]->GetType();
        // If T&&, cast.
        // Else, build a T&& from the U then cast that. Use this instead of BuildRvalueConstruction because we may need to preserve derived semantics.
        if (argty == analyzer.GetRvalueType(GetNonstaticMemberContext())) {
            args[0] = std::make_shared<LvalueCast>(args[0]);
        } else if (argty != analyzer.GetLvalueType(GetNonstaticMemberContext())) {
            args[0] = std::make_shared<LvalueCast>(analyzer.GetRvalueType(GetNonstaticMemberContext())->BuildValueConstruction({ args[0] }, c));
        }
    }
    return AdjustArgumentsForTypes(std::move(args), Args, c);
}
std::shared_ptr<Expression> FunctionSkeleton::GetStaticSelf() {
    struct Self : Expression {
        Self(Function* f) : f(f) {}
        Function* f;
        Type* GetType() override final { return f->GetSignature(); }
        llvm::Value* ComputeValue(CodegenContext& con) override final { return f->EmitCode(con); }
    };
    return std::make_shared<Self>(this);
}
std::string FunctionSkeleton::GetExportBody() {
    auto sig = GetSignature();
    auto import = "[import_name := \"" + llvmname + "\"]\n";
    if (!dynamic_cast<UserDefinedType*>(GetContext()))
        import += analyzer.GetTypeExport(GetContext());
    import += source_name;
    import += "(";
    unsigned i = 0;
    for (auto& ty : Args) {
        if (Args.size() == fun->args.size() + 1 && i == 0) {
            import += "this := ";
            ++i;
        } else {
            import += fun->args[i++].name + " := ";
        }
        if (&ty != &Args.back())
            import += analyzer.GetTypeExport(ty) + ", ";
        else
            import += analyzer.GetTypeExport(ty);
    }
    import += ")";
    import += " := " + analyzer.GetTypeExport(sig->GetReturnType()) + " { } \n";
    return import;
}

void FunctionSkeleton::AddDefaultHandlers(Analyzer& a) {
    AddHandler<const Parse::Return>(a.StatementHandlers, [](const Parse::Return* ret, Analyzer& analyzer, Type* self, Scope* current, std::function<std::function<Type*()>(Return*)> func) {
        struct ReturnStatement : public Statement, Return {
            Lexer::Range where;
            Type* self;
            std::shared_ptr<Expression> ret_expr;
            std::shared_ptr<Expression> build;
            std::function<Type*()> GetFunctionReturnType;

            Type* GetReturnType() override final {
                if (!ret_expr) return self->analyzer.GetVoidType();
                return ret_expr->GetType();
            }
            ReturnStatement(Type* f, std::shared_ptr<Expression> expr, Scope* current, Lexer::Range where, std::function<std::function<Type*()>(Return*)> func)
                : self(f), ret_expr(std::move(expr)), where(where)
            {
                GetFunctionReturnType = func(this);
                if (ret_expr) {
                    ListenToNode(ret_expr.get());
                }
                OnNodeChanged(ret_expr.get(), Change::Contents);
                ListenToNode(self);
            }
            void OnNodeChanged(Node* n, Change what) override final {
                if (GetFunctionReturnType() != self->analyzer.GetVoidType() && GetFunctionReturnType()) {
                    struct ReturnEmplaceValue : Expression {
                        ReturnEmplaceValue(std::function<Type*()> f)
                            : self(f) {}
                        std::function<Type*()> self;;
                        Type* GetType() override final {
                            return self()->analyzer.GetLvalueType(self());
                        }
                        llvm::Value* ComputeValue(CodegenContext& con) override final {
                            return con->GetInsertBlock()->getParent()->arg_begin();
                        }
                    };
                    build = Type::BuildInplaceConstruction(Wide::Memory::MakeUnique<ReturnEmplaceValue>(self), { ret_expr }, { self, where });
                } else
                    build = nullptr;
            }
            void GenerateCode(CodegenContext& con) override final {
                // Consider the simple cases first.
                // If our return type is void
                if (GetFunctionReturnType() == self->analyzer.GetVoidType()) {
                    // If we have a void-returning expression, evaluate it, destroy it, then return.
                    if (ret_expr) {
                        ret_expr->GetValue(con);
                    }
                    con.DestroyAll(false);
                    con->CreateRetVoid();
                    return;
                }

                // If we return a simple type
                if (!GetFunctionReturnType()->AlwaysKeepInMemory(con)) {
                    // and we already have an expression of that type
                    if (ret_expr->GetType() == GetFunctionReturnType()) {
                        // then great, just return it directly.
                        auto val = ret_expr->GetValue(con);
                        con.DestroyAll(false);
                        con->CreateRet(val);
                        return;
                    }
                    // If we have a reference to it, just load it right away.
                    if (ret_expr->GetType()->IsReference(GetFunctionReturnType())) {
                        auto val = con->CreateLoad(ret_expr->GetValue(con));
                        con.DestroyAll(false);
                        con->CreateRet(val);
                        return;
                    }
                    // Build will inplace construct this in our first argument, which is INCREDIBLY UNHELPFUL here.
                    // We would fix this up, but, cannot query the complexity of a type prior to code generation.
                    build = GetFunctionReturnType()->BuildValueConstruction({ ret_expr }, { self, where });
                    auto val = build->GetValue(con);
                    con.DestroyAll(false);
                    con->CreateRet(val);
                    return;
                }

                // If we return a complex type, the 0th parameter will be memory into which to place the return value.
                // build should be a function taking the memory and our ret's value and emplacing it.
                // Then return void.
                build->GetValue(con);
                con.DestroyAll(false);
                con->CreateRetVoid();
                return;
            }
        };
        return Wide::Memory::MakeUnique<ReturnStatement>(self, ret->RetExpr ? analyzer.AnalyzeExpression(self, ret->RetExpr.get()) : nullptr, current, ret->location, func);
    });

    AddHandler<Parse::CompoundStatement>(a.StatementHandlers, [](const Parse::CompoundStatement* comp, Analyzer& analyzer, Type* self, Scope* current, std::function<std::function<Type*()>(Return*)> func) {
        struct CompoundStatement : public Statement {
            CompoundStatement(Scope* s)
                : s(s) {}
            void GenerateCode(CodegenContext& con) {
                con.GenerateCodeAndDestroyLocals([this](CodegenContext& nested) {
                    for (auto&& stmt : s->active)
                        if (!nested.IsTerminated(nested->GetInsertBlock()))
                            stmt->GenerateCode(nested);
                });
            }
            Scope* s;
        };
        auto compound = new Scope(current);
        for (auto&& stmt : comp->stmts)
            compound->active.push_back(AnalyzeStatement(analyzer, stmt.get(), self, compound, func));
        return Wide::Memory::MakeUnique<CompoundStatement>(compound);
    });

    AddHandler<Parse::Variable>(a.StatementHandlers, [](const Parse::Variable* var, Analyzer& analyzer, Type* self, Scope* current, std::function<std::function<Type*()>(Return*)> func) {
        struct LocalVariable : public Expression {
            std::shared_ptr<Expression> construction;
            Wide::Util::optional<unsigned> tuple_num;
            Wide::Util::optional<Type*> explicit_type;
            Type* var_type = nullptr;
            std::shared_ptr<ImplicitTemporaryExpr> variable;
            std::shared_ptr<Expression> init_expr;
            std::function<void(CodegenContext&)> destructor;
            Type* self;
            Lexer::Range where;
            Lexer::Range init_where;

            void OnNodeChanged(Node* n, Change what) override final {
                if (explicit_type) return;
                if (what == Change::Destroyed) return;
                if (init_expr->GetType()) {
                    // If we're a value we handle it at codegen time.
                    auto newty = InferTypeFromExpression(init_expr.get(), true);
                    if (tuple_num) {
                        if (auto tupty = dynamic_cast<TupleType*>(newty)) {
                            auto tuple_access = tupty->PrimitiveAccessMember(init_expr, *tuple_num);
                            newty = tuple_access->GetType()->Decay();
                            variable = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(newty, Context{ self, where });
                            construction = Type::BuildInplaceConstruction(variable, { std::move(tuple_access) }, { self, init_where });
                            destructor = newty->BuildDestructorCall(variable, Context{ self, where }, true);
                            if (newty != var_type) {
                                var_type = newty;
                                OnChange();
                            }
                            return;
                        }
                        throw std::runtime_error("fuck");
                    }
                    if (!init_expr->GetType()->IsReference() && init_expr->GetType() == newty) {
                        if (init_expr->GetType() != var_type) {
                            var_type = newty;
                            OnChange();
                            return;
                        }
                    }
                    if (newty->IsReference()) {
                        var_type = newty;
                        OnChange();
                        return;
                    }
                    if (newty != var_type) {
                        if (newty) {
                            variable = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(newty, Context{ self, where });
                            construction = Type::BuildInplaceConstruction(variable, { init_expr }, { self, init_where });
                            destructor = newty->BuildDestructorCall(variable, Context{ self, where }, true);
                        }
                        var_type = newty;
                        OnChange();
                    }
                    return;
                }
                if (var_type) {
                    var_type = nullptr;
                    OnChange();
                }
            }
            LocalVariable(std::shared_ptr<Expression> ex, unsigned u, Type* self, Lexer::Range where, Lexer::Range init_where, TupleType* p = nullptr)
                : init_expr(std::move(ex)), tuple_num(u), self(self), where(where), init_where(init_where)
            {
                if (p) {
                    explicit_type = var_type = p->GetMembers()[u];
                    variable = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(var_type, Context{ self, where });
                    construction = Type::BuildInplaceConstruction(variable, { p->PrimitiveAccessMember(init_expr, u) }, { self, init_where });
                    destructor = var_type->BuildDestructorCall(variable, Context{ self, where }, true);
                }
                ListenToNode(init_expr.get());
                OnNodeChanged(init_expr.get(), Change::Contents);
            }
            LocalVariable(std::shared_ptr<Expression> ex, Type* self, Lexer::Range where, Lexer::Range init_where, Type* p = nullptr)
                : init_expr(std::move(ex)), self(self), where(where), init_where(init_where)
            {
                if (p) {
                    explicit_type = var_type = p;
                    variable = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(var_type, Context{ self, where });
                    construction = Type::BuildInplaceConstruction(variable, { init_expr }, { self, init_where });
                    destructor = var_type->BuildDestructorCall(variable, Context{ self, where }, true);
                }
                ListenToNode(init_expr.get());
                OnNodeChanged(init_expr.get(), Change::Contents);
            }
            llvm::Value* ComputeValue(CodegenContext& con) override final {
                if (init_expr->GetType() == var_type) {
                    // If they return a complex value by value, just steal it, and don't worry about destructors as it will already have handled it.
                    if (init_expr->GetType()->AlwaysKeepInMemory(con) || var_type->IsReference()) return init_expr->GetValue(con);
                    // If they return a simple by value, then we can just alloca it fine, no destruction needed.
                    auto alloc = con.CreateAlloca(var_type);
                    con->CreateStore(init_expr->GetValue(con), alloc);
                    return alloc;
                }

                construction->GetValue(con);
                if (!var_type->IsTriviallyDestructible())
                    con.AddDestructor(destructor);
                return variable->GetValue(con);
            }
            Type* GetType() override final {
                if (var_type) {
                    if (var_type->IsReference())
                        return self->analyzer.GetLvalueType(var_type->Decay());
                    return self->analyzer.GetLvalueType(var_type);
                }
                return nullptr;
            }
        };

        struct VariableStatement : public Statement {
            VariableStatement(std::vector<LocalVariable*> locs, std::shared_ptr<Expression> expr)
                : locals(std::move(locs)), init_expr(std::move(expr)){}
            std::shared_ptr<Expression> init_expr;
            std::vector<LocalVariable*> locals;

            void GenerateCode(CodegenContext& con) override final {
                init_expr->GetValue(con);
                for (auto local : locals)
                    local->GetValue(con);
            }
        };

        std::vector<LocalVariable*> locals;
        auto init_expr = analyzer.AnalyzeExpression(self, var->initializer.get());
        Type* var_type = nullptr;
        if (var->type) {
            auto expr = analyzer.AnalyzeExpression(self, var->type.get());
            auto conty = dynamic_cast<ConstructorType*>(expr->GetType()->Decay());
            if (!conty) throw std::runtime_error("Local variable type was not a type.");
            var_type = conty->GetConstructedType();
        }
        if (var->name.size() == 1) {
            auto&& name = var->name.front();
            if (current->named_variables.find(var->name.front().name) != current->named_variables.end())
                throw VariableShadowing(name.name, current->named_variables.at(name.name).second, name.where);
            auto var_stmt = Wide::Memory::MakeUnique<LocalVariable>(init_expr, self, var->name.front().where, var->initializer->location, var_type);
            locals.push_back(var_stmt.get());
            current->named_variables.insert(std::make_pair(name.name, std::make_pair(std::move(var_stmt), name.where)));
        } else {
            TupleType* tupty = nullptr;
            if (var_type) {
                tupty = dynamic_cast<TupleType*>(var_type);
                if (!tupty) throw std::runtime_error("The explicit type was not a tuple when there were multiple variables.");
                if (tupty->GetMembers().size() != var->name.size()) throw std::runtime_error("Explicit tuple type size did not match number of locals.");
            }
            unsigned i = 0;
            for (auto&& name : var->name) {
                if (current->named_variables.find(name.name) != current->named_variables.end())
                    throw VariableShadowing(name.name, current->named_variables.at(name.name).second, name.where);
                auto var_stmt = Wide::Memory::MakeUnique<LocalVariable>(init_expr, i++, self, name.where, var->initializer->location, tupty);
                locals.push_back(var_stmt.get());
                current->named_variables.insert(std::make_pair(name.name, std::make_pair(std::move(var_stmt), name.where)));
            }
        }
        return Wide::Memory::MakeUnique<VariableStatement>(std::move(locals), std::move(init_expr));
    });

    AddHandler<Parse::While>(a.StatementHandlers, [](const Parse::While* whil, Analyzer& analyzer, Type* self, Scope* current, std::function<std::function<Type*()>(Return*)> func) {
        struct WhileStatement : Statement, ControlFlowStatement {
            Type* self;
            Lexer::Range where;
            std::shared_ptr<Expression> cond;
            std::shared_ptr<Statement> body;
            std::shared_ptr<Expression> boolconvert;
            llvm::BasicBlock* continue_bb = nullptr;
            llvm::BasicBlock* check_bb = nullptr;
            CodegenContext* source_con = nullptr;
            CodegenContext* condition_con = nullptr;

            WhileStatement(std::shared_ptr<Expression> ex, Lexer::Range where, Type* s)
                : cond(std::move(ex)), where(where), self(s)
            {
                ListenToNode(cond.get());
                OnNodeChanged(cond.get(), Change::Contents);
            }
            void OnNodeChanged(Node* n, Change what) override final {
                if (what == Change::Destroyed) return;
                if (cond->GetType())
                    boolconvert = Type::BuildBooleanConversion(cond, { self, where });
            }
            void GenerateCode(CodegenContext& con) override final  {
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
        };
        auto condscope = new Scope(current);
        auto get_expr = [&, self]() -> std::shared_ptr<Expression> {
            if (whil->var_condition) {
                if (whil->var_condition->name.size() != 1)
                    throw std::runtime_error("fuck");
                condscope->active.push_back(AnalyzeStatement(analyzer, whil->var_condition.get(), self, condscope, func));
                return condscope->named_variables.begin()->second.first;
            }
            return analyzer.AnalyzeExpression(self, whil->condition.get());
        };
        auto cond = get_expr();
        auto while_stmt = Wide::Memory::MakeUnique<WhileStatement>(cond, whil->location, self);
        condscope->active.push_back(std::move(cond));
        condscope->control_flow = while_stmt.get();
        auto bodyscope = new Scope(condscope);
        bodyscope->active.push_back(AnalyzeStatement(analyzer, whil->body.get(), self, bodyscope, func));
        while_stmt->body = bodyscope->active.back();
        return std::move(while_stmt);
    });

    AddHandler<Parse::Continue>(a.StatementHandlers, [](const Parse::Continue* continue_stmt, Analyzer& analyzer, Type* self, Scope* current, std::function<std::function<Type*()>(Return*)>) {
        struct ContinueStatement : public Statement  {
            ContinueStatement(Scope* s)
                : control(s->GetCurrentControlFlow()) {}
            ControlFlowStatement* control;

            void GenerateCode(CodegenContext& con) {
                control->JumpForContinue(con);
            }
        };
        if (!current->GetCurrentControlFlow())
            throw NoControlFlowStatement(continue_stmt->location);
        return Wide::Memory::MakeUnique<ContinueStatement>(current);
    });

    AddHandler<Parse::Break>(a.StatementHandlers, [](const Parse::Break* break_stmt, Analyzer& analyzer, Type* self, Scope* current, std::function<std::function<Type*()>(Return*)>) {
        struct BreakStatement : public Statement {
            BreakStatement(Scope* s) : control(s->GetCurrentControlFlow()) {}
            ControlFlowStatement* control;
            void GenerateCode(CodegenContext& con) override final {
                control->JumpForBreak(con);
            }
        };
        if (!current->GetCurrentControlFlow())
            throw NoControlFlowStatement(break_stmt->location);
        return Wide::Memory::MakeUnique<BreakStatement>(current);
    });

    AddHandler<Parse::If>(a.StatementHandlers, [](const Parse::If* if_stmt, Analyzer& analyzer, Type* self, Scope* current, std::function<std::function<Type*()>(Return*)> func) {
        struct IfStatement : Statement {
            Type* self;
            Lexer::Range where;
            std::shared_ptr<Expression> cond;
            Statement* true_br;
            Statement* false_br;
            std::shared_ptr<Expression> boolconvert;

            IfStatement(std::shared_ptr<Expression> cond, Statement* true_b, Statement* false_b, Lexer::Range where, Type* s)
                : cond(std::move(cond)), true_br(std::move(true_b)), false_br(std::move(false_b)), where(where), self(s)
            {
                ListenToNode(this->cond.get());
                OnNodeChanged(this->cond.get(), Change::Contents);
            }
            void OnNodeChanged(Node* n, Change what) override final {
                if (what == Change::Destroyed) return;
                if (cond->GetType())
                    boolconvert = Type::BuildBooleanConversion(cond, { self, where });
            }
            void GenerateCode(CodegenContext& con) override final  {
                auto true_bb = llvm::BasicBlock::Create(con, "true_bb", con->GetInsertBlock()->getParent());
                auto continue_bb = llvm::BasicBlock::Create(con, "continue_bb", con->GetInsertBlock()->getParent());
                auto else_bb = false_br ? llvm::BasicBlock::Create(con->getContext(), "false_bb", con->GetInsertBlock()->getParent()) : continue_bb;
                con.GenerateCodeAndDestroyLocals([this, true_bb, continue_bb, else_bb](CodegenContext& condition_con) {
                    auto condition = boolconvert->GetValue(condition_con);
                    if (condition->getType() == llvm::Type::getInt8Ty(condition_con))
                        condition = condition_con->CreateTrunc(condition, llvm::Type::getInt1Ty(condition_con));
                    condition_con->CreateCondBr(condition, true_bb, else_bb);
                    condition_con->SetInsertPoint(true_bb);
                    condition_con.GenerateCodeAndDestroyLocals([this, continue_bb](CodegenContext& true_con) {
                        true_br->GenerateCode(true_con);
                    });
                    if (!condition_con.IsTerminated(condition_con->GetInsertBlock()))
                        condition_con->CreateBr(continue_bb);
                    if (false_br) {
                        condition_con->SetInsertPoint(else_bb);
                        condition_con.GenerateCodeAndDestroyLocals([this, continue_bb](CodegenContext& false_con) {
                            false_br->GenerateCode(false_con);
                        });
                        if (!condition_con.IsTerminated(condition_con->GetInsertBlock()))
                            condition_con->CreateBr(continue_bb);
                    }
                    condition_con->SetInsertPoint(continue_bb);
                });
            }
        };
        auto condscope = new Scope(current);
        auto get_expr = [&, self]() -> std::shared_ptr<Expression> {
            if (if_stmt->var_condition) {
                if (if_stmt->var_condition->name.size() != 1)
                    throw std::runtime_error("fuck");
                condscope->active.push_back(AnalyzeStatement(analyzer, if_stmt->var_condition.get(), self, condscope, func));
                return condscope->named_variables.begin()->second.first;
            }
            return analyzer.AnalyzeExpression(self, if_stmt->condition.get());
        };
        auto cond = get_expr();
        condscope->active.push_back(cond);
        Statement* true_br = nullptr;
        {
            auto truescope = new Scope(condscope);
            truescope->active.push_back(AnalyzeStatement(analyzer, if_stmt->true_statement.get(), self, truescope, func));
            true_br = truescope->active.back().get();
        }
        Statement* false_br = nullptr;
        if (if_stmt->false_statement) {
            auto falsescope = new Scope(condscope);
            falsescope->active.push_back(AnalyzeStatement(analyzer, if_stmt->false_statement.get(), self, falsescope, func));
            false_br = falsescope->active.back().get();
        }
        return Wide::Memory::MakeUnique<IfStatement>(cond, true_br, false_br, if_stmt->location, self);
    });

    AddHandler<Parse::Throw>(a.StatementHandlers, [](const Parse::Throw* thro, Analyzer& analyzer, Type* self, Scope* current, std::function<std::function<Type*()>(Return*)> func) -> std::unique_ptr<Statement> {
        struct ThrowStatement : public Statement {
            std::function<void(CodegenContext&)> throw_func;
            ThrowStatement(std::shared_ptr<Expression> expr, Context c) {
                throw_func = Semantic::ThrowObject(expr, c);
            }
            void GenerateCode(CodegenContext& con) override final {
                throw_func(con);
            }
        };
        struct RethrowStatement : public Statement {
            void GenerateCode(CodegenContext& con) override final {
                if (con.HasDestructors() || con.EHHandler)
                    con->CreateInvoke(con.GetCXARethrow(), con.GetUnreachableBlock(), con.CreateLandingpadForEH());
                else
                    con->CreateCall(con.GetCXARethrow());
            }
        };
        if (!thro->expr)
            return Wide::Memory::MakeUnique<RethrowStatement>();
        auto expression = analyzer.AnalyzeExpression(self, thro->expr.get());
        return Wide::Memory::MakeUnique<ThrowStatement>(std::move(expression), Context(self, thro->location));
    });

    AddHandler<Parse::TryCatch>(a.StatementHandlers, [](const Parse::TryCatch* try_, Analyzer& analyzer, Type* self, Scope* current, std::function<std::function<Type*()>(Return*)> func) {
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
            void GenerateCode(CodegenContext& con){
                auto source_block = con->GetInsertBlock();

                auto try_con = con;
                auto catch_block = llvm::BasicBlock::Create(con, "catch_block", con->GetInsertBlock()->getParent());
                auto dest_block = llvm::BasicBlock::Create(con, "dest_block", con->GetInsertBlock()->getParent());

                con->SetInsertPoint(catch_block);
                auto phi = con->CreatePHI(con.GetLpadType(), 0);
                std::vector<llvm::Constant*> rttis;
                for (auto&& catch_ : catches) {
                    if (catch_.t)
                        rttis.push_back(llvm::cast<llvm::Constant>(con->CreatePointerCast(catch_.RTTI(con), con.GetInt8PtrTy())));
                }
                try_con.EHHandler = CodegenContext::EHScope{ &con, catch_block, phi, rttis };
                try_con->SetInsertPoint(source_block);

                for (auto&& stmt : statements)
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
                for (auto&& catch_ : catches) {
                    CodegenContext catch_block_con(catch_con);
                    if (!catch_.t) {
                        for (auto&& stmt : catch_.stmts)
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
                    auto except = catch_block_con->CreateCall(catch_block_con.GetCXABeginCatch(), { except_object });
                    catch_.catch_param->param = except;
                    // Ensure __cxa_end_catch is called.
                    catch_block_con.AddDestructor(catch_ender);

                    for (auto&& stmt : catch_.stmts)
                        if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock()))
                            stmt->GenerateCode(catch_block_con);
                    if (!catch_block_con.IsTerminated(catch_block_con->GetInsertBlock())) {
                        con.DestroyDifference(catch_block_con, false);
                        catch_block_con->CreateBr(dest_block);
                    }
                    catch_con->SetInsertPoint(catch_continue);
                }
                // If we had no catch all, then we need to clean up and rethrow to the next try.
                if (catches.back().t) {
                    auto except = catch_con->CreateCall(catch_con.GetCXABeginCatch(), { except_object });
                    catch_con.AddDestructor(catch_ender);
                    catch_con->CreateInvoke(catch_con.GetCXARethrow(), catch_con.GetUnreachableBlock(), catch_con.CreateLandingpadForEH());
                }
                con->SetInsertPoint(dest_block);
            }
        };
        std::vector<std::shared_ptr<Statement>> try_stmts;
        {
            auto tryscope = new Scope(current);
            for (auto&& stmt : try_->statements->stmts)
                try_stmts.push_back(AnalyzeStatement(analyzer, stmt.get(), self, tryscope, func));
        }
        std::vector<TryStatement::Catch> catches;
        for (auto&& catch_ : try_->catches) {
            auto catchscope = new Scope(current);
            if (catch_.all) {
                std::vector<std::shared_ptr<Statement>> stmts;
                for (auto&& stmt : catch_.statements)
                    stmts.push_back(AnalyzeStatement(analyzer, stmt.get(), self, catchscope, func));
                catches.push_back(TryStatement::Catch(nullptr, std::move(stmts), nullptr));
                break;
            }
            auto type = analyzer.AnalyzeExpression(self, catch_.type.get());
            auto con = dynamic_cast<ConstructorType*>(type->GetType());
            if (!con) throw std::runtime_error("Catch parameter type was not a type.");
            auto catch_type = con->GetConstructedType();
            if (!IsLvalueType(catch_type) && !dynamic_cast<PointerType*>(catch_type))
                throw std::runtime_error("Attempted to catch by non-lvalue and nonpointer.");
            auto target_type = IsLvalueType(catch_type)
                ? catch_type->Decay()
                : dynamic_cast<PointerType*>(catch_type)->GetPointee();
            auto catch_parameter = std::make_shared<TryStatement::CatchParameter>();
            catch_parameter->t = catch_type;
            catchscope->named_variables.insert(std::make_pair(catch_.name, std::make_pair(catch_parameter, catch_.type->location)));
            std::vector<std::shared_ptr<Statement>> stmts;
            for (auto&& stmt : catch_.statements)
                stmts.push_back(AnalyzeStatement(analyzer, stmt.get(), self, catchscope, func));
            catches.push_back(TryStatement::Catch(catch_type, std::move(stmts), std::move(catch_parameter)));
        }
        return Wide::Memory::MakeUnique<TryStatement>(std::move(try_stmts), std::move(catches), analyzer);
    });
}