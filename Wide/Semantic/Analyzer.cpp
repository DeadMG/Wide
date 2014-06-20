#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Parser/AST.h>
#include <Wide/Parser/ASTVisitor.h>
#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/StringType.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/ClangNamespace.h>
#include <Wide/Semantic/Void.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/ClangInclude.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Bool.h>
#include <Wide/Semantic/ClangTemplateClass.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/TemplateType.h>
#include <Wide/Semantic/NullType.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Semantic/FloatType.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/LambdaType.h>
#include <Wide/Semantic/ArrayType.h>
#include <Wide/Util/Codegen/InitializeLLVM.h>
#include <Wide/Util/DebugUtilities.h>
#include <Wide/Semantic/Expression.h>
#include <sstream>
#include <iostream>
#include <unordered_set>
#include <fstream>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetRegistry.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

namespace {
    llvm::DataLayout GetDataLayout(std::string triple) {
        Util::InitializeLLVM();
        std::unique_ptr<llvm::TargetMachine> targetmachine;
        std::string err;
        const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(triple, err);
        llvm::TargetOptions targetopts;
        targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(triple, llvm::Triple(triple).getArchName(), "", targetopts));
        return llvm::DataLayout(targetmachine->getDataLayout()->getStringRepresentation());
    }
}

// After definition of type
Analyzer::~Analyzer() {}

Analyzer::Analyzer(const Options::Clang& opts, const AST::Module* GlobalModule)
: clangopts(&opts)
, QuickInfo([](Lexer::Range, Type*) {})
, ParameterHighlight([](Lexer::Range){})
, layout(::GetDataLayout(opts.TargetOptions.Triple))
{
    
    struct PointerCastType : OverloadResolvable, Callable {
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
            if (types.size() != 2) return Util::none;
            auto conty = dynamic_cast<ConstructorType*>(types[0]->Decay());
            if (!conty) return Util::none;
            if (!dynamic_cast<PointerType*>(conty->GetConstructedType())) return Util::none;
            if (!dynamic_cast<PointerType*>(types[1]->Decay())) return Util::none;
            return types;
        }
        Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { return this; }
        std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
            struct PointerCast : Expression {
                PointerCast(Type* t, std::unique_ptr<Expression> type, std::unique_ptr<Expression> arg)
                : to(t), arg(BuildValue(std::move(arg))), type(std::move(type)) {}
                Type* to;
                std::unique_ptr<Expression> arg;
                std::unique_ptr<Expression> type;

                Type* GetType() override final { return to; }
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    type->GetValue(con);
                    auto val = arg->GetValue(con);
                    return con->CreatePointerCast(val, to->GetLLVMType(con));
                }
            };
            auto conty = dynamic_cast<ConstructorType*>(args[0]->GetType()->Decay());
            assert(conty); // OR should not pick us if this is invalid.
            return Wide::Memory::MakeUnique<PointerCast>(conty->GetConstructedType(), std::move(args[0]), std::move(args[1]));
        }
        std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
            return args; 
        }
    };

    struct MoveType : OverloadResolvable, Callable {
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final { 
            if (types.size() == 1) return types; 
            return Util::none; 
        }
        Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) override final { 
            return this; 
        }
        std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
            return args; 
        }
        std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
            return Wide::Memory::MakeUnique<RvalueCast>(std::move(args[0]));
        }
    };

    global = GetWideModule(GlobalModule, nullptr);
    EmptyOverloadSet = Wide::Memory::MakeUnique<OverloadSet>(std::unordered_set<OverloadResolvable*>(), nullptr, *this);
    ClangInclude = Wide::Memory::MakeUnique<ClangIncludeEntity>(*this);
    Void = Wide::Memory::MakeUnique<VoidType>(*this);
    Boolean = Wide::Memory::MakeUnique<Bool>(*this);
    null = Wide::Memory::MakeUnique<NullType>(*this);
    PointerCast = Wide::Memory::MakeUnique<PointerCastType>();
    Move = Wide::Memory::MakeUnique<MoveType>();

    auto context = Context{ global, Lexer::Range(std::make_shared<std::string>("Analyzer internal.")) };
    global->AddSpecialMember("cpp", ClangInclude->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("void", GetConstructorType(Void.get())->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("global", global->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("int8", GetConstructorType(GetIntegralType(8, true))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("uint8", GetConstructorType(GetIntegralType(8, false))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("int16", GetConstructorType(GetIntegralType(16, true))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("uint16", GetConstructorType(GetIntegralType(16, false))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("int32", GetConstructorType(GetIntegralType(32, true))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("uint32", GetConstructorType(GetIntegralType(32, false))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("int64", GetConstructorType(GetIntegralType(64, true))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("uint64", GetConstructorType(GetIntegralType(64, false))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("float32", GetConstructorType(GetFloatType(32))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("float64", GetConstructorType(GetFloatType(64))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("bool", GetConstructorType(Boolean.get())->BuildValueConstruction(Expressions(), context));

    global->AddSpecialMember("byte", GetConstructorType(GetIntegralType(8, false))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("int", GetConstructorType(GetIntegralType(32, true))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("short", GetConstructorType(GetIntegralType(16, true))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("long", GetConstructorType(GetIntegralType(64, true))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("float", GetConstructorType(GetFloatType(32))->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("double", GetConstructorType(GetFloatType(64))->BuildValueConstruction(Expressions(), context));

    global->AddSpecialMember("null", GetNullType()->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("reinterpret_cast", GetOverloadSet(PointerCast.get())->BuildValueConstruction(Expressions(), context));
    global->AddSpecialMember("move", GetOverloadSet(Move.get())->BuildValueConstruction(Expressions(), context));

    auto str = "";
    llvm::SmallVector<char, 30> fuck_out_parameters;
    auto error = llvm::sys::fs::createTemporaryFile("", "", fuck_out_parameters);
    std::string path(fuck_out_parameters.begin(), fuck_out_parameters.end());
    std::ofstream file(path, std::ios::out);
    file << str;
    file.flush();
    file.close();
    AggregateCPPHeader(path, context.where);

    AddExpressionHandler<AST::String>([](Analyzer& a, Type* lookup, const AST::String* str) {
        return Wide::Memory::MakeUnique<String>(str->val, a);
    });

    AddExpressionHandler<AST::MemberAccess>([](Analyzer& a, Type* lookup, const AST::MemberAccess* memaccess) -> std::unique_ptr<Expression> {
        struct MemberAccess : Expression {
            MemberAccess(Type* l, Analyzer& an, const AST::MemberAccess* mem, std::unique_ptr<Expression> obj)
            : lookup(l), a(an), ast_node(mem), object(std::move(obj))
            {
                ListenToNode(object.get());
                OnNodeChanged(object.get(), Change::Contents);
            }
            std::unique_ptr<Expression> object;
            std::unique_ptr<Expression> access;
            const AST::MemberAccess* ast_node;
            Analyzer& a;
            Type* lookup;

            void OnNodeChanged(Node* n, Change what) override final {
                if (what == Change::Destroyed) return;
                auto currty = GetType();
                if (object->GetType()) {
                    access = object->GetType()->AccessMember(Wide::Memory::MakeUnique<ExpressionReference>(object.get()), ast_node->mem, Context{ lookup, ast_node->location });
                    if (!access)
                        throw NoMember(object->GetType(), lookup, ast_node->mem, ast_node->location);
                }
                else
                    access = nullptr;
                if (currty != GetType())
                    OnChange();
            }

            Type* GetType() override final {
                if (access)
                    return access->GetType();
                return nullptr;
            }

            llvm::Value* ComputeValue(CodegenContext& con) override final {
                return access->GetValue(con);
            }
        };
        return Wide::Memory::MakeUnique<MemberAccess>(lookup, a, memaccess, a.AnalyzeExpression(lookup, memaccess->expr));
    });

    AddExpressionHandler<AST::FunctionCall>([](Analyzer& a, Type* lookup, const AST::FunctionCall* call) -> std::unique_ptr<Expression> {
        struct FunctionCall : Expression {
            FunctionCall(Type* from, Lexer::Range where, std::unique_ptr<Expression> obj, std::vector<std::unique_ptr<Expression>> params)
            : object(std::move(obj)), args(std::move(params)), from(from), where(where)
            {
                ListenToNode(object.get());
                for (auto&& arg : args)
                    ListenToNode(arg.get());
                OnNodeChanged(object.get(), Change::Contents);
            }
            Lexer::Range where;
            Type* from;

            std::vector<std::unique_ptr<Expression>> args;
            std::unique_ptr<Expression> object;
            std::unique_ptr<Expression> call;

            void OnNodeChanged(Node* n, Change what) override final {
                if (what == Change::Destroyed) return;
                if (n == call.get()) { OnChange(); return; }
                auto ty = GetType();
                if (object) {
                    std::vector<std::unique_ptr<Expression>> refargs;
                    for (auto&& arg : args) {
                        if (arg->GetType())
                            refargs.push_back(Wide::Memory::MakeUnique<ExpressionReference>(arg.get()));
                    }
                    if (refargs.size() == args.size()) {
                        auto objty = object->GetType();
                        call = objty->BuildCall(Wide::Memory::MakeUnique<ExpressionReference>(object.get()), std::move(refargs), Context{ from, where });
                        ListenToNode(call.get());
                    }
                    else
                        throw std::runtime_error("fuck");
                }
                else
                    throw std::runtime_error("fuck");
                if (ty != GetType())
                    OnChange();
            }

            Type* GetType() override final {
                if (call)
                    return call->GetType();
                return nullptr;
            }

            llvm::Value* ComputeValue(CodegenContext& con) override final {
                return call->GetValue(con);
            }
            Expression* GetImplementation() { return call.get(); }
        };
        std::vector<std::unique_ptr<Expression>> args;
        for (auto arg : call->args)
            args.push_back(a.AnalyzeExpression(lookup, arg));
        return Wide::Memory::MakeUnique<FunctionCall>(lookup, call->location, a.AnalyzeExpression(lookup, call->callee), std::move(args));
    });

    AddExpressionHandler<AST::Identifier>([](Analyzer& a, Type* lookup, const AST::Identifier* ident) -> std::unique_ptr<Expression> {
        struct IdentifierLookup : public Expression {
            std::unique_ptr<Expression> LookupIdentifier(Type* context) {
                if (!context) return nullptr;
                context = context->Decay();
                if (auto fun = dynamic_cast<Function*>(context)) {
                    auto lookup = fun->LookupLocal(val);
                    if (lookup) return lookup;
                    if (auto lam = dynamic_cast<LambdaType*>(context->GetContext())) {
                        auto self = fun->LookupLocal("this");
                        auto result = lam->LookupCapture(std::move(self), val);
                        if (result)
                            return std::move(result);
                        return LookupIdentifier(context->GetContext()->GetContext());
                    }
                    if (auto member = fun->GetNonstaticMemberContext()) {
                        auto self = fun->LookupLocal("this");
                        auto result = self->GetType()->AccessMember(std::move(self), val, { self->GetType(), location });
                        if (result)
                            return std::move(result);
                        return LookupIdentifier(context->GetContext()->GetContext());
                    }
                    return LookupIdentifier(context->GetContext());
                }
                if (auto mod = dynamic_cast<Module*>(context)) {
                    // Module lookups shouldn't present any unknown types. They should only present constant contexts.
                    auto local_mod_instance = mod->BuildValueConstruction(Expressions(), Context{ context, location });
                    auto result = local_mod_instance->GetType()->AccessMember(std::move(local_mod_instance), val, { lookup, location });
                    if (!result) return LookupIdentifier(mod->GetContext());
                    if (!dynamic_cast<OverloadSet*>(result->GetType()))
                        return result;
                    auto lookup2 = LookupIdentifier(mod->GetContext());
                    if (!lookup2)
                        return result;
                    if (!dynamic_cast<OverloadSet*>(lookup2->GetType()))
                        return result;
                    return a.GetOverloadSet(dynamic_cast<OverloadSet*>(result->GetType()), dynamic_cast<OverloadSet*>(lookup2->GetType()))->BuildValueConstruction(Expressions(), Context{ context, location });
                }
                if (auto udt = dynamic_cast<UserDefinedType*>(context)) {
                    return LookupIdentifier(context->GetContext());
                }
                if (auto result = context->AccessMember(context->BuildValueConstruction(Expressions(), { lookup, location }), val, { lookup, location }))
                    return result;
                return LookupIdentifier(context->GetContext());
            }
            IdentifierLookup(const AST::Identifier* id, Analyzer& an, Type* lookup)
                : a(an), location(id->location), val(id->val), lookup(lookup)
            {
                OnNodeChanged(lookup);
            }
            Analyzer& a;
            std::string val;
            Lexer::Range location;
            Type* lookup;
            std::unique_ptr<Expression> result;
            void OnNodeChanged(Node* n) {
                auto ty = GetType();
                if (n == result.get()) { OnChange(); return; }
                result = LookupIdentifier(lookup);
                if (result)
                    ListenToNode(result.get());
                else
                    throw NoMember(lookup, lookup, val, location);
                if (ty != GetType())
                    OnChange();
            }
            Type* GetType() {
                return result ? result->GetType() : nullptr;
            }
            llvm::Value* ComputeValue(CodegenContext& con) override final {
                return result->GetValue(con);
            }
        };
        return Wide::Memory::MakeUnique<IdentifierLookup>(ident, a, lookup);
    });

    AddExpressionHandler<AST::True>([](Analyzer& a, Type* lookup, const AST::True* tru) {
        return Wide::Memory::MakeUnique<Semantic::Boolean>(true, a);
    });

    AddExpressionHandler<AST::False>([](Analyzer& a, Type* lookup, const AST::False* fals) {
        return Wide::Memory::MakeUnique<Semantic::Boolean>(false, a);
    });

    AddExpressionHandler<AST::This>([](Analyzer& a, Type* lookup, const AST::This* thi) {
        AST::Identifier i("this", thi->location);
        return a.AnalyzeExpression(lookup, &i);
    });

    AddExpressionHandler<AST::Type>([](Analyzer& a, Type* lookup, const AST::Type* ty) {
        auto udt = a.GetUDT(ty, lookup->GetConstantContext() ? lookup->GetConstantContext() : lookup->GetContext(), "anonymous");
        return a.GetConstructorType(udt)->BuildValueConstruction(Expressions(), { lookup, ty->where.front() });
    });

    AddExpressionHandler<AST::AddressOf>([](Analyzer& a, Type* lookup, const AST::AddressOf* addr) {
        return Wide::Memory::MakeUnique<ImplicitAddressOf>(a.AnalyzeExpression(lookup, addr->ex), Context(lookup, addr->location));
    });

    AddExpressionHandler<AST::Integer>([](Analyzer& a, Type* lookup, const AST::Integer* integer) {
        return Wide::Memory::MakeUnique<Integer>(llvm::APInt(64, std::stoll(integer->integral_value), true), a);
    });

    AddExpressionHandler<AST::BinaryExpression>([](Analyzer& a, Type* lookup, const AST::BinaryExpression* bin) {
        auto lhs = a.AnalyzeExpression(lookup, bin->lhs);
        auto rhs = a.AnalyzeExpression(lookup, bin->rhs);
        auto ty = lhs->GetType();
        return ty->BuildBinaryExpression(std::move(lhs), std::move(rhs), bin->type, { lookup, bin->location });
    });

    AddExpressionHandler<AST::Dereference>([](Analyzer& a, Type* lookup, const AST::Dereference* deref) {
        auto expr = a.AnalyzeExpression(lookup, deref->ex);
        auto ty = expr->GetType();
        return ty->BuildUnaryExpression(std::move(expr), Wide::Lexer::TokenType::Dereference, { lookup, deref->location });
    });

    AddExpressionHandler<AST::Negate>([](Analyzer& a, Type* lookup, const AST::Negate* neg) {
        auto expr = a.AnalyzeExpression(lookup, neg->ex);
        auto ty = expr->GetType();
        return ty->BuildUnaryExpression(std::move(expr), Lexer::TokenType::Negate, { lookup, neg->location });
    });

    AddExpressionHandler<AST::Increment>([](Analyzer& a, Type* lookup, const AST::Increment* inc) {
        auto expr = a.AnalyzeExpression(lookup, inc->ex);
        auto ty = expr->GetType();
        if (inc->postfix) {
            auto copy = ty->Decay()->BuildValueConstruction(Expressions(Wide::Memory::MakeUnique<ExpressionReference>(expr.get())), { lookup, inc->location });
            auto result = ty->Decay()->BuildUnaryExpression(std::move(expr), Lexer::TokenType::Increment, { lookup, inc->location });
            return BuildChain(std::move(copy), BuildChain(std::move(result), Wide::Memory::MakeUnique<ExpressionReference>(copy.get())));
        }
        return ty->Decay()->BuildUnaryExpression(std::move(expr), Lexer::TokenType::Increment, { lookup, inc->location });
    });

    AddExpressionHandler<AST::Tuple>([](Analyzer& a, Type* lookup, const AST::Tuple* tup) {
        std::vector<std::unique_ptr<Expression>> exprs;
        for (auto elem : tup->expressions)
            exprs.push_back(a.AnalyzeExpression(lookup, elem));
        std::vector<Type*> types;
        for (auto&& expr : exprs)
            types.push_back(expr->GetType()->Decay());
        return a.GetTupleType(types)->ConstructFromLiteral(std::move(exprs), { lookup, tup->location });
    });

    AddExpressionHandler<AST::PointerMemberAccess>([](Analyzer& a, Type* lookup, const AST::PointerMemberAccess* paccess) {
        auto obj = a.AnalyzeExpression(lookup, paccess->ex);
        auto objty = obj->GetType();
        auto subobj = objty->BuildUnaryExpression(std::move(obj), Lexer::TokenType::Dereference, { lookup, paccess->location });
        return subobj->GetType()->AccessMember(std::move(subobj), paccess->member, { lookup, paccess->location });
    });

    AddExpressionHandler<AST::Decltype>([](Analyzer& a, Type* lookup, const AST::Decltype* declty) {
        auto expr = a.AnalyzeExpression(lookup, declty->ex);
        return a.GetConstructorType(expr->GetType())->BuildValueConstruction(Expressions(), { lookup, declty->location });
    });

    AddExpressionHandler<AST::Typeid>([](Analyzer& a, Type* lookup, const AST::Typeid* rtti) -> std::unique_ptr<Expression> {
        auto expr = a.AnalyzeExpression(lookup, rtti->ex);
        auto tu = expr->GetType()->analyzer.AggregateCPPHeader("typeinfo", rtti->location);
        auto global_namespace = expr->GetType()->analyzer.GetClangNamespace(*tu, tu->GetDeclContext());
        auto std_namespace = global_namespace->AccessMember(global_namespace->BuildValueConstruction(Expressions(), { lookup, rtti->location }), "std", { lookup, rtti->location });
        assert(std_namespace && "<typeinfo> didn't have std namespace?");
        auto std_namespace_ty = std_namespace->GetType();
        auto clangty = std_namespace_ty->AccessMember(std::move(std_namespace), "type_info", { lookup, rtti->location });
        assert(clangty && "<typeinfo> didn't have std::type_info?");
        auto conty = dynamic_cast<ConstructorType*>(clangty->GetType()->Decay());
        assert(conty && "<typeinfo>'s std::type_info wasn't a type?");
        auto result = conty->analyzer.GetLvalueType(conty->GetConstructedType());
        // typeid(T)
        if (auto ty = dynamic_cast<ConstructorType*>(expr->GetType()->Decay())) {
            struct RTTI : public Expression {
                RTTI(Type* ty, Type* result) : ty(ty), result(result) {}
                Type* ty;
                Type* result;
                Type* GetType() override final { return result; }
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    return con->CreateBitCast(ty->GetRTTI(con), result->GetLLVMType(con));
                }
            };
            return Wide::Memory::MakeUnique<RTTI>(ty->GetConstructedType(), result);
        }
        // typeid(expr)
        struct RTTI : public Expression {
            RTTI(std::unique_ptr<Expression> arg, Type* result) : expr(std::move(arg)), result(result)
            {
                // If we have a polymorphic type, find the RTTI entry, if applicable.
                ty = expr->GetType()->Decay();
                vtable = ty->GetVtableLayout();
                if (!vtable.layout.empty()) {
                    expr = ty->GetVirtualPointer(std::move(expr));
                    for (unsigned int i = 0; i < vtable.layout.size(); ++i) {
                        if (auto spec = boost::get<Type::VTableLayout::SpecialMember>(&vtable.layout[i].function)) {
                            if (*spec == Type::VTableLayout::SpecialMember::RTTIPointer) {
                                rtti_offset = i - vtable.offset;
                                break;
                            }
                        }
                    }
                }
            }
            std::unique_ptr<Expression> expr;
            Type::VTableLayout vtable;
            Wide::Util::optional<unsigned> rtti_offset;
            Type* result;
            Type* ty;
            Type* GetType() override final { return result; }
            llvm::Value* ComputeValue(CodegenContext& con) override final {
                // Do we have a vtable offset? If so, use the RTTI entry there. The expr will already be a pointer to the vtable pointer.
                if (rtti_offset) {
                    auto vtable_pointer = con->CreateLoad(expr->GetValue(con));
                    auto rtti_pointer = con->CreateLoad(con->CreateConstGEP1_32(vtable_pointer, *rtti_offset));
                    return con->CreateBitCast(rtti_pointer, result->GetLLVMType(con));
                }
                return con->CreateBitCast(ty->GetRTTI(con), result->GetLLVMType(con));
            }
        };
        return Wide::Memory::MakeUnique<RTTI>(std::move(expr), result);
    });
    
    AddExpressionHandler<AST::Lambda>([](Analyzer& a, Type* lookup, const AST::Lambda* lam) {
        std::vector<std::unordered_set<std::string>> lambda_locals;

        // Only implicit captures.
        std::unordered_set<std::string> captures;
        struct LambdaVisitor : AST::Visitor<LambdaVisitor> {
            std::vector<std::unordered_set<std::string>>* lambda_locals;
            std::unordered_set<std::string>* captures;
            void VisitVariableStatement(const AST::Variable* v) {
                for (auto&& name : v->name)
                    lambda_locals->back().insert(name.name);
            }
            void VisitLambdaCapture(const AST::Variable* v) {
                for (auto&& name : v->name)
                    lambda_locals->back().insert(name.name);
            }
            void VisitLambdaArgument(const AST::FunctionArgument* arg) {
                lambda_locals->back().insert(arg->name);
            }
            void VisitLambda(const AST::Lambda* l) {
                lambda_locals->emplace_back();
                for (auto&& x : l->args)
                    VisitLambdaArgument(&x);
                for (auto&& x : l->Captures)
                    VisitLambdaCapture(x);
                lambda_locals->emplace_back();
                for (auto&& x : l->statements)
                    VisitStatement(x);
                lambda_locals->pop_back();
                lambda_locals->pop_back();
            }
            void VisitIdentifier(const AST::Identifier* e) {
                for (auto&& scope : *lambda_locals)
                if (scope.find(e->val) != scope.end())
                    return;
                captures->insert(e->val);
            }
            void VisitCompoundStatement(const AST::CompoundStatement* cs) {
                lambda_locals->emplace_back();
                for (auto&& x : cs->stmts)
                    VisitStatement(x);
                lambda_locals->pop_back();
            }
            void VisitWhileStatement(const AST::While* wh) {
                lambda_locals->emplace_back();
                VisitExpression(wh->condition);
                VisitStatement(wh->body);
                lambda_locals->pop_back();
            }
            void VisitIfStatement(const AST::If* br) {
                lambda_locals->emplace_back();
                VisitExpression(br->condition);
                lambda_locals->emplace_back();
                VisitStatement(br->true_statement);
                lambda_locals->pop_back();
                lambda_locals->pop_back();
                lambda_locals->emplace_back();
                VisitStatement(br->false_statement);
                lambda_locals->pop_back();
            }
        };
        LambdaVisitor l;
        l.captures = &captures;
        l.lambda_locals = &lambda_locals;
        l.VisitLambda(lam);

        Context c(lookup, lam->location);
        // We obviously don't want to capture module-scope names.
        // Only capture from the local scope, and from "this".
        {
            auto caps = std::move(captures);
            for (auto&& name : caps) {
                if (auto fun = dynamic_cast<Function*>(lookup)) {
                    if (fun->LookupLocal(name))
                        captures.insert(name);
                    if (auto udt = dynamic_cast<UserDefinedType*>(fun->GetContext())) {
                        if (udt->HasMember(name))
                            captures.insert(name);
                    }
                }
            }
        }

        // Just as a double-check, eliminate all explicit captures from the list. This should never have any effect
        // but I'll hunt down any bugs caused by eliminating it later.
        for (auto&& arg : lam->Captures)
        for (auto&& name : arg->name)
            captures.erase(name.name);

        std::vector<std::pair<std::string, std::unique_ptr<Expression>>> cap_expressions;
        for (auto&& arg : lam->Captures) {
            cap_expressions.push_back(std::make_pair(arg->name.front().name, a.AnalyzeExpression(lookup, arg->initializer)));
        }
        for (auto&& name : captures) {
            AST::Identifier ident(name, lam->location);
            cap_expressions.push_back(std::make_pair(name, a.AnalyzeExpression(lookup, &ident)));
        }
        std::vector<std::pair<std::string, Type*>> types;
        std::vector<std::unique_ptr<Expression>> expressions;
        for (auto&& cap : cap_expressions) {
            if (!lam->defaultref)
                types.push_back(std::make_pair(cap.first, cap.second->GetType()->Decay()));
            else {
                auto IsImplicitCapture = [&]() {
                    return captures.find(cap.first) != captures.end();
                };
                if (IsImplicitCapture()) {
                    if (!cap.second->GetType()->IsReference())
                        assert(false); // how the fuck
                    types.push_back(std::make_pair(cap.first, cap.second->GetType()));
                }
                else {
                    types.push_back(std::make_pair(cap.first, cap.second->GetType()->Decay()));
                }
            }
            expressions.push_back(std::move(cap.second));
        }
        auto type = a.GetLambdaType(lam, types, lookup->GetConstantContext() ? lookup->GetConstantContext() : lookup->GetContext());
        return type->BuildLambdaFromCaptures(std::move(expressions), c);
    });

    AddExpressionHandler<AST::DynamicCast>([](Analyzer& a, Type* lookup, const AST::DynamicCast* dyn_cast) -> std::unique_ptr<Expression> {
        auto type = a.AnalyzeExpression(lookup, dyn_cast->type);
        auto object = a.AnalyzeExpression(lookup, dyn_cast->object);

        auto dynamic_cast_to_void = [&](PointerType* baseptrty) -> std::unique_ptr<Expression> {
            // Load it from the vtable if it actually has one.
            auto layout = baseptrty->GetPointee()->GetVtableLayout();
            if (layout.layout.size() == 0) {
                throw std::runtime_error("dynamic_casted to void* a non-polymorphic type.");
            }
            for (unsigned int i = 0; i < layout.layout.size(); ++i) {
                if (auto spec = boost::get<Type::VTableLayout::SpecialMember>(&layout.layout[i].function)) {
                    if (*spec == Type::VTableLayout::SpecialMember::OffsetToTop) {
                        auto offset = i - layout.offset;
                        auto vtable = baseptrty->GetPointee()->GetVirtualPointer(Wide::Memory::MakeUnique<ExpressionReference>(object.get()));
                        struct DynamicCastToVoidPointer : Expression {
                            DynamicCastToVoidPointer(unsigned off, std::unique_ptr<Expression> obj, std::unique_ptr<Expression> vtable)
                            : vtable_offset(off), vtable(std::move(vtable)), object(std::move(obj)) {}
                            unsigned vtable_offset;
                            std::unique_ptr<Expression> vtable;
                            std::unique_ptr<Expression> object;
                            llvm::Value* ComputeValue(CodegenContext& con) override final {
                                auto obj_ptr = object->GetValue(con);
                                llvm::BasicBlock* source_bb = con->GetInsertBlock();
                                llvm::BasicBlock* nonnull_bb = llvm::BasicBlock::Create(con, "nonnull_bb", source_bb->getParent());
                                llvm::BasicBlock* continue_bb = llvm::BasicBlock::Create(con, "continue_bb", source_bb->getParent());
                                con->CreateCondBr(con->CreateIsNull(obj_ptr), continue_bb, nonnull_bb);
                                con->SetInsertPoint(nonnull_bb);
                                auto vtable_ptr = con->CreateLoad(vtable->GetValue(con));
                                auto ptr_to_offset = con->CreateConstGEP1_32(vtable_ptr, vtable_offset);
                                auto offset = con->CreateLoad(ptr_to_offset);
                                auto result = con->CreateGEP(con->CreateBitCast(obj_ptr, con.GetInt8PtrTy()), offset);
                                con->CreateBr(continue_bb);
                                con->SetInsertPoint(continue_bb);
                                auto phi = con->CreatePHI(con.GetInt8PtrTy(), 2);
                                phi->addIncoming(llvm::Constant::getNullValue(con.GetInt8PtrTy()), source_bb);
                                phi->addIncoming(result, nonnull_bb);
                                return phi;
                            }
                            Type* GetType() override final {
                                auto&& a = object->GetType()->analyzer;
                                return a.GetPointerType(a.GetVoidType());
                            }
                        };
                        return Wide::Memory::MakeUnique<DynamicCastToVoidPointer>(offset, std::move(object), std::move(vtable));
                    }
                }
            }
            throw std::runtime_error("Attempted to cast to void*, but the object's vtable did not carry an offset to top member.");
        };

        auto polymorphic_dynamic_cast = [&](PointerType* basety, PointerType* derty) -> std::unique_ptr<Expression> {
            struct PolymorphicDynamicCast : Expression {
                PolymorphicDynamicCast(Type* basety, Type* derty, std::unique_ptr<Expression> object)
                : basety(basety), derty(derty), object(std::move(object)) {}
                std::unique_ptr<Expression> object;
                Type* basety;
                Type* derty;
                Type* GetType() override final {
                    return derty->analyzer.GetPointerType(derty);
                }
                llvm::Value* ComputeValue(CodegenContext& con) override final {
                    auto obj_ptr = object->GetValue(con);
                    llvm::BasicBlock* source_bb = con->GetInsertBlock();
                    llvm::BasicBlock* nonnull_bb = llvm::BasicBlock::Create(con, "nonnull_bb", source_bb->getParent());
                    llvm::BasicBlock* continue_bb = llvm::BasicBlock::Create(con, "continue_bb", source_bb->getParent());
                    con->CreateCondBr(con->CreateIsNull(obj_ptr), continue_bb, nonnull_bb);
                    con->SetInsertPoint(nonnull_bb);
                    auto dynamic_cast_func = con.module->getFunction("__dynamic_cast");
                    auto ptrdiffty = llvm::IntegerType::get(con, basety->analyzer.GetDataLayout().getPointerSize());
                    if (!dynamic_cast_func) {
                        llvm::Type* args[] = { con.GetInt8PtrTy(), con.GetInt8PtrTy(), con.GetInt8PtrTy(), ptrdiffty };
                        auto functy = llvm::FunctionType::get(llvm::Type::getVoidTy(con), args, false);
                        dynamic_cast_func = llvm::Function::Create(functy, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "__dynamic_cast", con);
                    }
                    llvm::Value* args[] = { obj_ptr, basety->GetRTTI(con), derty->GetRTTI(con), llvm::ConstantInt::get(ptrdiffty, (uint64_t)-1, true) };
                    auto result = con->CreateCall(dynamic_cast_func, args, "");
                    con->CreateBr(continue_bb);
                    con->SetInsertPoint(continue_bb);
                    auto phi = con->CreatePHI(con.GetInt8PtrTy(), 2);
                    phi->addIncoming(llvm::Constant::getNullValue(derty->GetLLVMType(con)), source_bb);
                    phi->addIncoming(result, nonnull_bb);
                    return con->CreatePointerCast(phi, GetType()->GetLLVMType(con));
                }
            };
            return Wide::Memory::MakeUnique<PolymorphicDynamicCast>(basety->GetPointee(), derty->GetPointee(), std::move(object));
        };

        if (auto con = dynamic_cast<ConstructorType*>(type->GetType()->Decay())) {
            // Only support pointers right now
            if (auto derptrty = dynamic_cast<PointerType*>(con->GetConstructedType())) {
                if (auto baseptrty = dynamic_cast<PointerType*>(object->GetType()->Decay())) {
                    // derived-to-base conversion- doesn't require calling the routine
                    if (baseptrty->GetPointee()->IsDerivedFrom(derptrty->GetPointee()) == Type::InheritanceRelationship::UnambiguouslyDerived) {
                        return derptrty->BuildValueConstruction(Expressions(std::move(object)), { lookup, dyn_cast->location });
                    }

                    // void*
                    if (derptrty->GetPointee() == a.GetVoidType()) {
                        return dynamic_cast_to_void(baseptrty);
                    }

                    // polymorphic
                    if (baseptrty->GetPointee()->GetVtableLayout().layout.empty())
                        throw std::runtime_error("Attempted dynamic_cast on non-polymorphic base.");

                    return polymorphic_dynamic_cast(baseptrty, derptrty);
                }
            }
        }
        throw std::runtime_error("Used unimplemented dynamic_cast functionality.");
    });

    AddExpressionHandler<AST::Index>([](Analyzer& a, Type* lookup, const AST::Index* index) {
        auto obj = a.AnalyzeExpression(lookup, index->object);
        auto ind = a.AnalyzeExpression(lookup, index->index);
        auto ty = obj->GetType();
        return ty->BuildIndex(std::move(obj), std::move(ind), { lookup, index->location });
    });
}


ClangTU* Analyzer::LoadCPPHeader(std::string file, Lexer::Range where) {
    if (headers.find(file) != headers.end())
        return &headers.find(file)->second;
    headers.insert(std::make_pair(file, ClangTU(file, *clangopts, where, *this)));
    auto ptr = &headers.find(file)->second;
    return ptr;
}
ClangTU* Analyzer::AggregateCPPHeader(std::string file, Lexer::Range where) {
    if (!AggregateTU) {
        AggregateTU = Wide::Memory::MakeUnique<ClangTU>(file, *clangopts, where, *this);
        auto ptr = AggregateTU.get();
        return ptr;
    }
    AggregateTU->AddFile(file, where);
    return AggregateTU.get();
}

void Analyzer::GenerateCode(llvm::Module* module) {
    if (AggregateTU)
        AggregateTU->GenerateCodeAndLinkModule(module, layout);
    for (auto&& tu : headers)
        tu.second.GenerateCodeAndLinkModule(module, layout);
    for (auto&& set : WideFunctions)
        for (auto&& signature : set.second)
            signature.second->EmitCode(module);
}

Type* Analyzer::GetClangType(ClangTU& from, clang::QualType t) {
    t = t.getCanonicalType();
    if (t.isConstQualified())
       t.removeLocalConst();
    if (t->isLValueReferenceType()) {
        return GetLvalueType(GetClangType(from, t->getAs<clang::LValueReferenceType>()->getPointeeType()));
    }
    if (t->isRValueReferenceType()) {
        return GetRvalueType(GetClangType(from, t->getAs<clang::RValueReferenceType>()->getPointeeType()));
    }
    if (t->isBooleanType())
        return Boolean.get();
    if (t->isIntegerType())
        return GetIntegralType(from.GetASTContext().getIntWidth(t), t->isSignedIntegerType());
    if (t->isVoidType())
        return Void.get();
    if (t->isNullPtrType())
        return null.get();
    if (t->isPointerType())
        return GetPointerType(GetClangType(from, t->getPointeeType()));
    if (t->isBooleanType())
        return Boolean.get();
    if (GeneratedClangTypes.find(t) != GeneratedClangTypes.end())
        return GeneratedClangTypes[t];
    if (ClangTypes.find(t) == ClangTypes.end())
        ClangTypes[t] = Wide::Memory::MakeUnique<ClangType>(&from, t, *this);
    return ClangTypes[t].get();
}
void Analyzer::AddClangType(clang::QualType t, Type* match) {
    if (GeneratedClangTypes.find(t) != GeneratedClangTypes.end())
        assert(false);
    GeneratedClangTypes[t] = match;
}

ClangNamespace* Analyzer::GetClangNamespace(ClangTU& tu, clang::DeclContext* con) {
    assert(con);
    if (ClangNamespaces.find(con) == ClangNamespaces.end())
        ClangNamespaces[con] = Wide::Memory::MakeUnique<ClangNamespace>(con, &tu, *this);
    return ClangNamespaces[con].get();
}

FunctionType* Analyzer::GetFunctionType(Type* ret, const std::vector<Type*>& t, bool variadic) {
    if (FunctionTypes.find(ret) == FunctionTypes.end()
     || FunctionTypes[ret].find(t) == FunctionTypes[ret].end()
     || FunctionTypes[ret][t].find(variadic) == FunctionTypes[ret][t].end())
        FunctionTypes[ret][t][variadic] = Wide::Memory::MakeUnique<FunctionType>(ret, t, *this, variadic);
    return FunctionTypes[ret][t][variadic].get();
}

Function* Analyzer::GetWideFunction(const AST::FunctionBase* p, Type* context, const std::vector<Type*>& types, std::string name) {
    if (WideFunctions.find(p) == WideFunctions.end()
     || WideFunctions[p].find(types) == WideFunctions[p].end())
        WideFunctions[p][types] = Wide::Memory::MakeUnique<Function>(types, p, *this, context, name);
    return WideFunctions[p][types].get();
}

Module* Analyzer::GetWideModule(const AST::Module* p, Module* higher) {
    if (WideModules.find(p) == WideModules.end())
        WideModules[p] = Wide::Memory::MakeUnique<Module>(p, higher, *this);
    return WideModules[p].get();
}

LvalueType* Analyzer::GetLvalueType(Type* t) {
    if (t == Void.get())
        assert(false);

    if (LvalueTypes.find(t) == LvalueTypes.end())
        LvalueTypes[t] = Wide::Memory::MakeUnique<LvalueType>(t, *this);
    
    return LvalueTypes[t].get();
}

Type* Analyzer::GetRvalueType(Type* t) {    
    if (t == Void.get())
        assert(false);
    
    if (RvalueTypes.find(t) != RvalueTypes.end())
        return RvalueTypes[t].get();
    
    if (auto rval = dynamic_cast<RvalueType*>(t))
        return rval;

    if (auto lval = dynamic_cast<LvalueType*>(t))
        return lval;

    RvalueTypes[t] = Wide::Memory::MakeUnique<RvalueType>(t, *this);
    return RvalueTypes[t].get();
}

ConstructorType* Analyzer::GetConstructorType(Type* t) {
    if (ConstructorTypes.find(t) == ConstructorTypes.end())
        ConstructorTypes[t] = Wide::Memory::MakeUnique<ConstructorType>(t, *this);
    return ConstructorTypes[t].get();
}

ClangTemplateClass* Analyzer::GetClangTemplateClass(ClangTU& from, clang::ClassTemplateDecl* decl) {
    if (ClangTemplateClasses.find(decl) == ClangTemplateClasses.end())
        ClangTemplateClasses[decl] = Wide::Memory::MakeUnique<ClangTemplateClass>(decl, &from, *this);
    return ClangTemplateClasses[decl].get();
}

OverloadSet* Analyzer::GetOverloadSet(const AST::FunctionOverloadSet* set, Type* t, std::string name) {
    std::unordered_set<OverloadResolvable*> resolvable;
    for (auto x : set->functions)
        resolvable.insert(GetCallableForFunction(x, t, name));
    return GetOverloadSet(resolvable, t);
}
OverloadSet* Analyzer::GetOverloadSet() {
    return EmptyOverloadSet.get();
}
OverloadSet* Analyzer::GetOverloadSet(OverloadResolvable* c) {
    std::unordered_set<OverloadResolvable*> set;
    set.insert(c);
    return GetOverloadSet(set);
}
OverloadSet* Analyzer::GetOverloadSet(std::unordered_set<OverloadResolvable*> set, Type* nonstatic) {
    if (callable_overload_sets.find(set) != callable_overload_sets.end())
        if (callable_overload_sets[set].find(nonstatic) != callable_overload_sets[set].end())
            return callable_overload_sets[set][nonstatic].get();
    if (nonstatic && (dynamic_cast<UserDefinedType*>(nonstatic->Decay()) || dynamic_cast<ClangType*>(nonstatic->Decay())))
        callable_overload_sets[set][nonstatic] = Wide::Memory::MakeUnique<OverloadSet>(set, nonstatic, *this);
    else
        callable_overload_sets[set][nonstatic] = Wide::Memory::MakeUnique<OverloadSet>(set, nullptr, *this);
    return callable_overload_sets[set][nonstatic].get();
}

UserDefinedType* Analyzer::GetUDT(const AST::Type* t, Type* context, std::string name) {
    if (UDTs.find(t) == UDTs.end()
     || UDTs[t].find(context) == UDTs[t].end()) {
        UDTs[t][context] = Wide::Memory::MakeUnique<UserDefinedType>(t, *this, context, name);
    }
    return UDTs[t][context].get();
}
IntegralType* Analyzer::GetIntegralType(unsigned bits, bool sign) {
    if (integers.find(bits) == integers.end()
     || integers[bits].find(sign) == integers[bits].end()) {
        integers[bits][sign] = Wide::Memory::MakeUnique<IntegralType>(bits, sign, *this);
    }
    return integers[bits][sign].get();
}
PointerType* Analyzer::GetPointerType(Type* to) {
    if (Pointers.find(to) == Pointers.end())
        Pointers[to] = Wide::Memory::MakeUnique<PointerType>(to, *this);
    return Pointers[to].get();
}
TupleType* Analyzer::GetTupleType(std::vector<Type*> types) {
    if (tupletypes.find(types) == tupletypes.end())
        tupletypes[types] = Wide::Memory::MakeUnique<TupleType>(types, *this);
    return tupletypes[types].get();
}
Type* Analyzer::GetNullType() {
    return null.get();
}
Type* Analyzer::GetBooleanType() {
    return Boolean.get();
}
Type* Analyzer::GetVoidType() {
    return Void.get();
}
/*Type* Analyzer::GetNothingFunctorType() {
    return NothingFunctor;
}*/

#pragma warning(disable : 4800)
bool Semantic::IsLvalueType(Type* t) {
    return dynamic_cast<LvalueType*>(t);
}
bool Semantic::IsRvalueType(Type* t) {
    return dynamic_cast<RvalueType*>(t);
}
#pragma warning(default : 4800)
OverloadSet* Analyzer::GetOverloadSet(OverloadSet* f, OverloadSet* s, Type* context) {
    if (CombinedOverloadSets[f].find(s) != CombinedOverloadSets[f].end())
        return CombinedOverloadSets[f][s].get();
    if (CombinedOverloadSets[s].find(f) != CombinedOverloadSets[s].end())
        return CombinedOverloadSets[s][f].get();
    CombinedOverloadSets[f][s] = Wide::Memory::MakeUnique<OverloadSet>(f, s, *this, context);
    return CombinedOverloadSets[f][s].get();
}
FloatType* Analyzer::GetFloatType(unsigned bits) {
    if (FloatTypes.find(bits) == FloatTypes.end())
        FloatTypes[bits] = Wide::Memory::MakeUnique<FloatType>(bits, *this);
    return FloatTypes[bits].get();
}
Module* Analyzer::GetGlobalModule() {
    return global;
}
OverloadSet* Analyzer::GetOverloadSet(std::unordered_set<clang::NamedDecl*> decls, ClangTU* from, Type* context) {
    if (clang_overload_sets.find(decls) == clang_overload_sets.end()
     || clang_overload_sets[decls].find(context) == clang_overload_sets[decls].end()) {
        clang_overload_sets[decls][context] = Wide::Memory::MakeUnique<OverloadSet>(decls, from, context, *this);
    }
    return clang_overload_sets[decls][context].get();
}
OverloadResolvable* Analyzer::GetCallableForFunction(const AST::FunctionBase* f, Type* context, std::string name) {
    if (FunctionCallables.find(f) != FunctionCallables.end())
        return FunctionCallables.at(f).get();

    struct FunctionCallable : public OverloadResolvable {
        FunctionCallable(const AST::FunctionBase* f, Type* con, std::string str)
        : func(f), context(con), name(str) {}
        const AST::FunctionBase* func;
        Type* context;
        std::string name;

        bool HasImplicitThis() {
            // If we are a member without an explicit this, then we have an implicit this.
            if (!dynamic_cast<UserDefinedType*>(context->Decay()) && !dynamic_cast<LambdaType*>(context->Decay()))
                return false;
            if (func->args.size() > 0) {
                if (func->args[0].name == "this") {
                    return false;
                }
            }
            return true;
        }

        std::unordered_map<unsigned, ConstructorType*> lookups;

        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
            if (types.size() != func->args.size() + HasImplicitThis()) return Util::none;
            // If we are a member and we have an explicit this then treat the first normally.
            // Else if we are a member, blindly accept whatever is given for argument 0 as long as it's the member type.
            // Else, treat the first argument normally.

            std::vector<Type*> result;
            if (HasImplicitThis()) {
                if (IsLvalueType(types[0])) {
                    if (types[0]->IsA(types[0], a.GetLvalueType(context), GetAccessSpecifier(source, types[0]))) {
                        result.push_back(a.GetLvalueType(context));
                    } else
                        return Util::none;
                } else {
                    if (types[0]->IsA(types[0], a.GetRvalueType(context), GetAccessSpecifier(source, types[0]))) {
                        result.push_back(a.GetRvalueType(context));
                    } else
                        return Util::none;
                }
                types.erase(types.begin());
            }

            unsigned num = 0;
            for (auto argument : types) {
                auto context = this->context;
                auto get_con_type = [&]() -> ConstructorType* {
                    if (lookups.find(num) != lookups.end())
                        return lookups[num];

                    if (!func->args[num].type) {
                        a.ParameterHighlight(func->args[num].location);
                        return a.GetConstructorType(argument->Decay());
                    }

                    struct OverloadSetLookupContext : public MetaType {
                        OverloadSetLookupContext(Analyzer& a) : MetaType(a) {}
                        Type* context;
                        Type* member;
                        Type* argument;
                        std::unique_ptr<Expression> AccessMember(std::unique_ptr<Expression> t, std::string name, Context c) override final {
                            if (name == "this") {
                                if (member)
                                    return analyzer.GetConstructorType(member->Decay())->BuildValueConstruction(Expressions(), c);
                            }
                            if (name == "auto")
                                return analyzer.GetConstructorType(argument->Decay())->BuildValueConstruction(Expressions(), c);
                            return nullptr;
                        }
                        Type* GetContext() override final {
                            return context;
                        }
                        std::string explain() override final { return context->explain(); }
                    };

                    OverloadSetLookupContext lc(a);
                    lc.argument = argument;
                    lc.context = context;
                    lc.member = dynamic_cast<UserDefinedType*>(context->Decay());
                    auto p_type = a.AnalyzeExpression(&lc, func->args[num].type)->GetType()->Decay();
                    auto con_type = dynamic_cast<ConstructorType*>(p_type);
                    if (!con_type)
                        throw Wide::Semantic::NotAType(p_type, func->args[num].type->location);
                    a.QuickInfo(func->args[num].location, con_type->GetConstructedType());
                    a.ParameterHighlight(func->args[num].location);
                    if (!IsMultiTyped(func->args[num]))
                        lookups[num] = con_type;
                    return con_type;
                };
                auto parameter_type = get_con_type();
                ++num;
                if (argument->IsA(argument, parameter_type->GetConstructedType(), GetAccessSpecifier(source, argument)))
                    result.push_back(parameter_type->GetConstructedType());
                else
                    return Util::none;
            }
            return result;
        }

        Callable* GetCallableForResolution(std::vector<Type*> types, Analyzer& a) override final {
            // If it's a dynamic function might not be the one we looked up.
            //if (auto function = dynamic_cast<const AST::Function*>(func)) {
            //    if (function->dynamic) {
            //        auto udt = dynamic_cast<UserDefinedType*>(context);
            //        return udt->GetCallableForDynamicCall(function, types, name);
            //    }
            //}
            return a.GetWideFunction(func, context, std::move(types), name);
        }
    };
    FunctionCallables[f] = Wide::Memory::MakeUnique<FunctionCallable>(f, context->Decay(), name);
    return FunctionCallables.at(f).get();
}

Lexer::Access Semantic::GetAccessSpecifier(Type* from, Type* to) {
    auto source = from->Decay();
    auto target = to->Decay();
    if (source == target) return Lexer::Access::Private;
    if (source->IsDerivedFrom(target) == Type::InheritanceRelationship::UnambiguouslyDerived)
        return Lexer::Access::Protected;
    if (auto func = dynamic_cast<Function*>(from)) {
        if (auto clangty = dynamic_cast<ClangType*>(to)) {
            Lexer::Access max = GetAccessSpecifier(func->GetContext(), target);
            for (auto clangcontext : func->GetClangContexts())
                max = std::max(max, GetAccessSpecifier(clangcontext, target));
            return max;
        }
    }
    if (auto context = source->GetContext())
        return GetAccessSpecifier(context, target);

    return Lexer::Access::Public;
}

void ProcessFunction(const AST::Function* f, Analyzer& a, Module* m, std::string name) {
    bool exported = false;
    for (auto attr : f->attributes) {
        if (auto ident = dynamic_cast<const AST::Identifier*>(attr.initialized))
            if (ident->val == "export")
                exported = true;
    }
    if (!exported) return;
    std::vector<Type*> types;
    for (auto arg : f->args) {
        if (!arg.type) return;
        auto expr = a.AnalyzeCachedExpression(m, arg.type);
        if (auto ty = dynamic_cast<ConstructorType*>(expr->GetType()->Decay()))
            types.push_back(ty->GetConstructedType());
        else
            return;
    }
    auto func = a.GetWideFunction(f, m, types, name);
    func->ComputeBody();
}
void ProcessOverloadSet(const AST::FunctionOverloadSet* set, Analyzer& a, Module* m, std::string name) {
    for (auto func : set->functions) {
        ProcessFunction(func, a, m, name);
    }
}

std::string Semantic::GetNameForOperator(Lexer::TokenType t) {
    return "";
}

void AnalyzeExportedFunctionsInModule(Analyzer& a, Module* m) {
    auto mod = m->GetASTModule();
    for (auto decl : mod->decls) {
        if (auto overset = dynamic_cast<const AST::FunctionOverloadSet*>(decl.second))
            ProcessOverloadSet(overset, a, m, decl.first);
    }
    for (auto overset : mod->opcondecls)
        ProcessOverloadSet(overset.second, a, m, GetNameForOperator(overset.first));
    for (auto decl : mod->decls)
        if (auto nested = dynamic_cast<const AST::Module*>(decl.second))
            AnalyzeExportedFunctionsInModule(a, a.GetWideModule(nested, m));
}
void Semantic::AnalyzeExportedFunctions(Analyzer& a) {
    AnalyzeExportedFunctionsInModule(a, a.GetGlobalModule());
}
OverloadResolvable* Analyzer::GetCallableForTemplateType(const AST::TemplateType* t, Type* context) {
    if (TemplateTypeCallables.find(t) != TemplateTypeCallables.end())
        return TemplateTypeCallables[t].get();

    struct TemplateTypeCallable : Callable {
        TemplateTypeCallable(Type* con, const Wide::AST::TemplateType* tempty, std::vector<Type*> args)
        : context(con), templatetype(tempty), types(args) {}
        Type* context;
        const Wide::AST::TemplateType* templatetype;
        std::vector<Type*> types;
        std::vector<std::unique_ptr<Expression>> AdjustArguments(std::vector<std::unique_ptr<Expression>> args, Context c) override final { return args; }
        std::unique_ptr<Expression> CallFunction(std::vector<std::unique_ptr<Expression>> args, Context c) override final {
            return context->analyzer.GetConstructorType(context->analyzer.GetTemplateType(templatetype, context, types, ""))->BuildValueConstruction(Expressions(), c);
        }
    };

    struct TemplateTypeResolvable : OverloadResolvable {
        TemplateTypeResolvable(const AST::TemplateType* f, Type* con)
        : templatetype(f), context(con) {}
        Type* context;
        const Wide::AST::TemplateType* templatetype;
        std::unordered_map<std::vector<Type*>, std::unique_ptr<TemplateTypeCallable>, VectorTypeHasher> Callables;

        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
            if (types.size() != templatetype->arguments.size()) return Util::none;
            std::vector<Type*> valid;
            for (unsigned num = 0; num < types.size(); ++num) {
                auto arg = types[num]->Decay()->GetConstantContext();
                if (!arg) return Util::none;
                if (!templatetype->arguments[num].type) {
                    a.ParameterHighlight(templatetype->arguments[num].location); 
                    valid.push_back(arg);
                    continue;
                }
                auto p_type = a.AnalyzeCachedExpression(context, templatetype->arguments[num].type)->GetType()->Decay();
                auto con_type = dynamic_cast<ConstructorType*>(p_type);
                if (!con_type)
                    throw Wide::Semantic::NotAType(p_type, templatetype->arguments[num].location);
                a.QuickInfo(templatetype->arguments[num].location, con_type->GetConstructedType());
                a.ParameterHighlight(templatetype->arguments[num].location);
                if (arg->IsA(arg, con_type->GetConstructedType(), GetAccessSpecifier(source, arg)))
                    valid.push_back(con_type->GetConstructedType());
                else
                    return Util::none;
            }
            return valid;
        }
        Callable* GetCallableForResolution(std::vector<Type*> types, Analyzer& a) override final { 
            if (Callables.find(types) != Callables.end())
                return Callables[types].get();
            Callables[types] = Wide::Memory::MakeUnique<TemplateTypeCallable>(context, templatetype, types);
            return Callables[types].get();
        }
    };

    TemplateTypeCallables[t] = Wide::Memory::MakeUnique<TemplateTypeResolvable>(t, context);
    return TemplateTypeCallables[t].get();
}

TemplateType* Analyzer::GetTemplateType(const Wide::AST::TemplateType* ty, Type* context, std::vector<Type*> arguments, std::string name) {
    if (WideTemplateInstantiations.find(ty) == WideTemplateInstantiations.end()
     || WideTemplateInstantiations[ty].find(arguments) == WideTemplateInstantiations[ty].end()) {

        name += "(";
        std::unordered_map<std::string, Type*> args;

        for (unsigned num = 0; num < ty->arguments.size(); ++num) {
            args[ty->arguments[num].name] = arguments[num];
            name += arguments[num]->explain();
            if (num != arguments.size() - 1)
                name += ", ";
        }
        name += ")";
        
        WideTemplateInstantiations[ty][arguments] = Wide::Memory::MakeUnique<TemplateType>(ty->t, *this, context, args, name);
    }
    return WideTemplateInstantiations[ty][arguments].get();
}

Type* Analyzer::GetTypeForString(std::string str) {
    if (LiteralStringTypes.find(str) == LiteralStringTypes.end())
        LiteralStringTypes[str] = Wide::Memory::MakeUnique<StringType>(str, *this);
    return LiteralStringTypes[str].get();
}
LambdaType* Analyzer::GetLambdaType(const AST::Lambda* lam, std::vector<std::pair<std::string, Type*>> types, Type* context) {
    if (LambdaTypes.find(lam) == LambdaTypes.end()
     || LambdaTypes[lam].find(types) == LambdaTypes[lam].end())
        LambdaTypes[lam][types] = Wide::Memory::MakeUnique<LambdaType>(types, lam, context, *this);
    return LambdaTypes[lam][types].get();
}
bool Semantic::IsMultiTyped(const AST::FunctionArgument& f) {
    if (!f.type) return true;
    struct Visitor : public AST::Visitor<Visitor> {
        bool auto_found = false;
        void VisitIdentifier(const AST::Identifier* i) {
            auto_found = auto_found || i->val == "auto";
        }
    };
    Visitor v;
    v.VisitExpression(f.type);
    return v.auto_found;
}
bool Semantic::IsMultiTyped(const AST::FunctionBase* f) {
    bool ret = false;
    for (auto&& var : f->args)
        ret = ret || IsMultiTyped(var);
    return ret;
}

std::unique_ptr<Expression> Analyzer::AnalyzeExpression(Type* lookup, const AST::Expression* e) {
    static_assert(std::is_polymorphic<AST::Expression>::value, "Expression must be polymorphic.");
    auto&& type_info = typeid(*e);
    if (expression_handlers.find(type_info) != expression_handlers.end())
        return expression_handlers[type_info](*this, lookup, e);
    assert(false && "Attempted to analyze expression for which there was no handler.");
}
Type* Semantic::InferTypeFromExpression(Expression* e, bool local) {
    if (!local)
        if (auto con = dynamic_cast<ConstructorType*>(e->GetType()->Decay()))
            return con->GetConstructedType();
    if (auto explicitcon = dynamic_cast<ExplicitConstruction*>(e)) {
        return explicitcon->GetType();
    }
    return e->GetType()->Decay();
}
std::unique_ptr<Expression> Analyzer::AnalyzeCachedExpression(Type* lookup, const AST::Expression* e) {
    if (ExpressionCache.find(e) == ExpressionCache.end())
        ExpressionCache[e] = AnalyzeExpression(lookup, e);
    return Wide::Memory::MakeUnique<ExpressionReference>(ExpressionCache[e].get());
}
ClangTU* Analyzer::GetAggregateTU() {
    return AggregateTU.get();
}

ArrayType* Analyzer::GetArrayType(Type* t, unsigned num) {
    if (ArrayTypes.find(t) == ArrayTypes.end()
        || ArrayTypes[t].find(num) == ArrayTypes[t].end())
        ArrayTypes[t][num] = Wide::Memory::MakeUnique<ArrayType>(*this, t, num);
    return ArrayTypes[t][num].get();
}