#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Parser/AST.h>
#include <Wide/Parser/ASTVisitor.h>
#include <Wide/Codegen/Generator.h>
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
#include <Wide/Util/DebugUtilities.h>
#include <Wide/Semantic/Expression.h>
#include <sstream>
#include <iostream>
#include <unordered_set>

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
        Codegen::InitializeLLVM();
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
                llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                    type->GetValue(g, bb);
                    auto val = arg->GetValue(g, bb);
                    return bb.CreatePointerCast(val, to->GetLLVMType(g));
                }
                void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                    arg->DestroyLocals(g, bb);
                    type->DestroyLocals(g, bb);
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
}

    /*
        // Ugly to perform an AST-level transformation in the analyzer
        // But hey- the AST exists to represent the exact source.
        void VisitLambda(const AST::Lambda* lam) {
            // Need to not-capture things that would be available anyway.
            
            std::vector<std::unordered_set<std::string>> lambda_locals;
            // Only implicit captures.
            std::unordered_set<std::string> captures;
            struct LambdaVisitor : AST::Visitor<LambdaVisitor> {
                std::vector<std::unordered_set<std::string>>* lambda_locals;
                std::unordered_set<std::string>* captures;
                void VisitVariableStatement(const AST::Variable* v) {
                    for(auto&& name : v->name)
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
                    for(auto&& x : l->args)
                        VisitLambdaArgument(&x);
                    for(auto&& x : l->Captures)
                        VisitLambdaCapture(x);
                    lambda_locals->emplace_back();
                    for(auto&& x : l->statements)
                        VisitStatement(x);
                    lambda_locals->pop_back();
                    lambda_locals->pop_back();
                }
                void VisitIdentifier(const AST::Identifier* e) {
                    for(auto&& scope : *lambda_locals)
                        if (scope.find(e->val) != scope.end())
                            return;
                    captures->insert(e->val);
                }
                void VisitCompoundStatement(const AST::CompoundStatement* cs) {
                    lambda_locals->emplace_back();
                    for(auto&& x : cs->stmts)
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

            Context c(*self, lam->location, handler, t);
            // We obviously don't want to capture module-scope names.
            // Only capture from the local scope, and from "this".
            {
                auto caps = std::move(captures);
                for (auto&& name : caps) {
                    if (auto fun = dynamic_cast<Function*>(t)) {
                        if (fun->LookupLocal(name, c))
                            captures.insert(name);
                        if (auto udt = dynamic_cast<UserDefinedType*>(fun->GetContext(*self))) {
                            if (udt->HasMember(name))
                                captures.insert(name);
                        }
                    }
                }
            }
            
            // Just as a double-check, eliminate all explicit captures from the list. This should never have any effect
            // but I'll hunt down any bugs caused by eliminating it later.
            for(auto&& arg : lam->Captures)
                for(auto&& name : arg->name)
                    captures.erase(name.name);

            std::vector<std::pair<std::string, ConcreteExpression>> cap_expressions;
            for(auto&& arg : lam->Captures) {
                cap_expressions.push_back(std::make_pair(arg->name.front().name, self->AnalyzeExpression(t, arg->initializer, handler)));
            }
            for(auto&& name : captures) {                
                AST::Identifier ident(name, lam->location);
                cap_expressions.push_back(std::make_pair(name, self->AnalyzeExpression(t, &ident, handler)));
            }
            std::vector<std::pair<std::string, Type*>> types;
            std::vector<ConcreteExpression> expressions;
            for (auto cap : cap_expressions) {
                types.push_back(std::make_pair(cap.first, cap.second.t->Decay()));
                expressions.push_back(cap.second);
            }
            auto type = self->arena.Allocate<LambdaType>(types, lam, *self);
            out = type->BuildLambdaFromCaptures(expressions, c);
        }
    };*/

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

void Analyzer::GenerateCode(Codegen::Generator& g) {
    if (AggregateTU)
        AggregateTU->GenerateCodeAndLinkModule(g);
    for (auto&& tu : headers)
        tu.second.GenerateCodeAndLinkModule(g);
    for (auto&& set : WideFunctions)
        for (auto&& signature : set.second)
            signature.second->EmitCode(g);
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
        return callable_overload_sets[set].get();
    if (nonstatic && (dynamic_cast<UserDefinedType*>(nonstatic->Decay()) || dynamic_cast<ClangType*>(nonstatic->Decay())))
        callable_overload_sets[set] = Wide::Memory::MakeUnique<OverloadSet>(set, nonstatic, *this);
    else
        callable_overload_sets[set] = Wide::Memory::MakeUnique<OverloadSet>(set, nullptr, *this);
    return callable_overload_sets[set].get();
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
                    auto p_type = AnalyzeExpression(&lc, func->args[num].type, a)->GetType()->Decay();
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
    if (auto base = dynamic_cast<BaseType*>(target)) {
        if (auto derived = dynamic_cast<BaseType*>(source)) {
            if (derived->IsDerivedFrom(target) == InheritanceRelationship::UnambiguouslyDerived)
                return Lexer::Access::Protected;
        }
    }
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
    for (auto stmt : f->prolog) {
        auto ass = dynamic_cast<const AST::BinaryExpression*>(stmt);
        if (!ass || ass->type != Lexer::TokenType::Assignment)
            continue;
        auto ident = dynamic_cast<const AST::Identifier*>(ass->lhs);
        if (!ident)
            continue;
        if (ident->val == "ExportName")
            exported = true;
        if (ident->val == "ExportAs")
            exported = true;
    }
    if (!exported) return;
    std::vector<Type*> types;
    for (auto arg : f->args) {
        if (!arg.type) return;
        auto expr = AnalyzeExpression(m, arg.type, a);
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

                auto p_type = AnalyzeExpression(context, templatetype->arguments[num].type, a)->GetType()->Decay();
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

std::unique_ptr<Expression> Semantic::AnalyzeExpression(Type* lookup, const AST::Expression* e, Analyzer& a) {
    if (auto str = dynamic_cast<const AST::String*>(e))
        return Wide::Memory::MakeUnique<String>(str->val, a);

    if (auto memaccess = dynamic_cast<const AST::MemberAccess*>(e)) {
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
                } else 
                    access = nullptr;
                if (currty != GetType())
                    OnChange();
            }

            Type* GetType() override final {
                if (access)
                    return access->GetType();
                return nullptr;
            }

            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                access->DestroyLocals(g, bb);
                object->DestroyLocals(g, bb);
            }
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                return access->GetValue(g, bb);
            }
        };
        return Wide::Memory::MakeUnique<MemberAccess>(lookup, a, memaccess, AnalyzeExpression(lookup, memaccess->expr, a));
    }

    if (auto call = dynamic_cast<const AST::FunctionCall*>(e)) {
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
                        call = object->GetType()->BuildCall(Wide::Memory::MakeUnique<ExpressionReference>(object.get()), std::move(refargs), Context{ from, where });
                        ListenToNode(call.get());
                    } else
                        call = nullptr;
                }
                else
                    call = nullptr;
                if (ty != GetType())
                    OnChange();
            }

            Type* GetType() override final {
                if (call)
                    return call->GetType();
                return nullptr;
            }

            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                // Let callee destroy args because we don't know exactly when to destroy them.
                // For example, consider arg := T(); f(T((), arg); which becomes f(T(), U(arg));. Here we would destroy in the wrong order.
                call->DestroyLocals(g, bb);
                object->DestroyLocals(g, bb);
            }
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                return call->GetValue(g, bb);
            }
            Expression* GetImplementation() { return call.get(); }
        };
        std::vector<std::unique_ptr<Expression>> args;
        for (auto arg : call->args)
            args.push_back(AnalyzeExpression(lookup, arg, a));
        return Wide::Memory::MakeUnique<FunctionCall>(lookup, call->location, AnalyzeExpression(lookup, call->callee, a), std::move(args));
    }

    if (auto ident = dynamic_cast<const AST::Identifier*>(e)) {
        struct IdentifierLookup : public Expression {
            std::unique_ptr<Expression> LookupIdentifier(Type* context) {
                if (!context) return nullptr;
                context = context->Decay();
                if (auto fun = dynamic_cast<Function*>(context)) {
                    auto lookup = fun->LookupLocal(val);
                    if (lookup) return lookup;
                    if (auto member = dynamic_cast<MemberFunctionContext*>(context->GetContext())) {
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
                if (ty != GetType())
                    OnChange();
            }
            Type* GetType() {
                return result ? result->GetType() : nullptr;
            }
            llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                return result->GetValue(g, bb);
            }
            void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
                result->DestroyLocals(g, bb);
            }
        };
        return Wide::Memory::MakeUnique<IdentifierLookup>(ident, a, lookup);
    }

    if (auto tru = dynamic_cast<const AST::True*>(e))
        return Wide::Memory::MakeUnique<Boolean>(true, a);
   
    if (auto fals = dynamic_cast<const AST::False*>(e))
        return Wide::Memory::MakeUnique<Boolean>(false, a);

    if (auto thi = dynamic_cast<const AST::This*>(e)) {
        AST::Identifier i("this", thi->location);
        return AnalyzeExpression(lookup, &i, a);
    }

    if (auto ty = dynamic_cast<const AST::Type*>(e)) {
        auto udt = a.GetUDT(ty, lookup->GetConstantContext() ? lookup->GetConstantContext() : lookup->GetContext(), "anonymous");
        return a.GetConstructorType(udt)->BuildValueConstruction(Expressions(), { lookup, ty->where.front() });
    }

    if (auto addr = dynamic_cast<const AST::AddressOf*>(e))
        return Wide::Memory::MakeUnique<ImplicitAddressOf>(AnalyzeExpression(lookup, addr->ex, a));

    if (auto integer = dynamic_cast<const AST::Integer*>(e))    
        return Wide::Memory::MakeUnique<Integer>(llvm::APInt(64, std::stoll(integer->integral_value), true), a);

    if (auto bin = dynamic_cast<const AST::BinaryExpression*>(e)) {
        auto lhs = AnalyzeExpression(lookup, bin->lhs, a);
        auto rhs = AnalyzeExpression(lookup, bin->rhs, a);
        auto ty = lhs->GetType();
        return ty->BuildBinaryExpression(std::move(lhs), std::move(rhs), bin->type, { lookup, bin->location });
    }

    if (auto deref = dynamic_cast<const AST::Dereference*>(e)) {
        auto expr = AnalyzeExpression(lookup, deref->ex, a);
        auto ty = expr->GetType();
        return ty->BuildUnaryExpression(std::move(expr), Wide::Lexer::TokenType::Dereference, { lookup, deref->location });
    }

    if (auto neg = dynamic_cast<const AST::Negate*>(e)) {
        auto expr = AnalyzeExpression(lookup, neg->ex, a);
        auto ty = expr->GetType();
        return ty->BuildUnaryExpression(std::move(expr), Lexer::TokenType::Negate, { lookup, neg->location });
    }

    if (auto inc = dynamic_cast<const AST::Increment*>(e)) {
        auto expr = AnalyzeExpression(lookup, inc->ex, a);
        auto ty = expr->GetType();
        if (inc->postfix) {
            auto copy = ty->Decay()->BuildValueConstruction(Expressions(Wide::Memory::MakeUnique<ExpressionReference>(expr.get())), { lookup, inc->location });
            auto result = ty->Decay()->BuildUnaryExpression(std::move(expr), Lexer::TokenType::Increment, { lookup, inc->location });
            return BuildChain(std::move(copy), BuildChain(std::move(result), Wide::Memory::MakeUnique<ExpressionReference>(copy.get())));
        }
        return ty->Decay()->BuildUnaryExpression(std::move(expr), Lexer::TokenType::Increment, { lookup, inc->location });
    }
    if (auto tup = dynamic_cast<const AST::Tuple*>(e)) {
        std::vector<std::unique_ptr<Expression>> exprs;
        for (auto elem : tup->expressions)
            exprs.push_back(AnalyzeExpression(lookup, elem, a));
        std::vector<Type*> types;
        for (auto&& expr : exprs)
            types.push_back(expr->GetType()->Decay());
        return a.GetTupleType(types)->ConstructFromLiteral(std::move(exprs), { lookup, tup->location });
    }
    if (auto paccess = dynamic_cast<const AST::PointerMemberAccess*>(e)) {
        auto obj = AnalyzeExpression(lookup, paccess->ex, a);
        auto objty = obj->GetType();
        auto subobj = objty->BuildUnaryExpression(std::move(obj), Lexer::TokenType::Dereference, { lookup, paccess->location });
        return subobj->GetType()->AccessMember(std::move(subobj), paccess->member, { lookup, paccess->location });
    }
    if (auto declty = dynamic_cast<const AST::Decltype*>(e)) {
        auto expr = AnalyzeExpression(lookup, declty->ex, a);
        return a.GetConstructorType(expr->GetType())->BuildValueConstruction(Expressions(), { lookup, declty->location });
    }
    assert(false);
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
