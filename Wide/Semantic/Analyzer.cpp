#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Parser/AST.h>
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
#include <Wide/Semantic/MemberDataPointerType.h>
#include <Wide/Semantic/MemberFunctionPointerType.h>
#include <Wide/Util/Codegen/InitializeLLVM.h>
#include <Wide/Semantic/Expression.h>
#include <sstream>
#include <iostream>
#include <unordered_set>
#include <fstream>
#include <boost/uuid/uuid_io.hpp>
#include <Wide/Util/Codegen/GetMCJITProcessTriple.h>
#include <Wide/Semantic/FunctionSkeleton.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <Wide/Util/Codegen/CreateModule.h>
#include <Wide/Util/Codegen/CloneFunctionIntoModule.h>

#pragma warning(push, 0)
#include <clang/AST/Type.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/AST.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <clang/Basic/LangOptions.h>
#include <llvm/Support/TargetRegistry.h>
#include <clang/Basic/TargetOptions.h>
#include <llvm/Linker/Linker.h>
#include <llvm/ExecutionEngine/GenericValue.h>
// Gotta include the header or creating JIT won't work... fucking LLVM.
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Target/TargetSubtargetInfo.h>
#include <llvm/IR/DiagnosticPrinter.h>
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
		auto info = std::unique_ptr<llvm::MCSubtargetInfo>(target.createMCSubtargetInfo(triple, "", ""));
        return llvm::DataLayout(targetmachine->getSubtargetImpl()->getDataLayout()->getStringRepresentation());
    }
}

// After definition of type
Analyzer::~Analyzer() {
    auto copy = errors;
    for (auto&& err : copy)
        err->disconnect();
}

Analyzer::Analyzer(const Options::Clang& opts, const Parse::Module* GlobalModule, llvm::LLVMContext& con, const std::unordered_map<std::string, std::string>& headers)
    : clangopts(&opts)
    , ImportHeaders(headers)
    , QuickInfo([](Lexer::Range, Type*) {})
    , ParameterHighlight([](Lexer::Range){})
    , layout(::GetDataLayout(opts.TargetOptions.Triple))
    , ConstantModule(Wide::Util::CreateModuleForTriple(Wide::Util::GetMCJITProcessTriple(), con))
{
    assert(opts.LanguageOptions.CXXExceptions);
    assert(opts.LanguageOptions.RTTI);
    struct PointerCastType : OverloadResolvable, Callable {
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
            if (types.size() != 2) return Util::none;
            auto conty = dynamic_cast<ConstructorType*>(types[0]->Decay());
            if (!conty) return Util::none;
            if (!dynamic_cast<PointerType*>(conty->GetConstructedType())) return Util::none;
            if (!dynamic_cast<PointerType*>(types[1]->Decay())) return Util::none;
            return types;
        }
        Callable* GetCallableForResolution(std::vector<Type*>, Type*, Analyzer& a) override final { return this; }
        std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            auto conty = dynamic_cast<ConstructorType*>(args[0]->GetType(key)->Decay());
            return CreatePrimGlobal(Range::Container(args), conty->GetConstructedType(), [=](CodegenContext& con) {
                args[0]->GetValue(con);
                auto val = args[1]->GetValue(con);
                return con->CreatePointerCast(val, conty->GetConstructedType()->GetLLVMType(con));
            });
        }
        std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            return args; 
        }
    };

    struct MoveType : OverloadResolvable, Callable {
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final { 
            if (types.size() == 1) return types; 
            return Util::none; 
        }
        Callable* GetCallableForResolution(std::vector<Type*>, Type*, Analyzer& a) override final {
            return this; 
        }
        std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            return args; 
        }
        std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            return Wide::Memory::MakeUnique<RvalueCast>(std::move(args[0]));
        }
    };

    global = GetWideModule(GlobalModule, nullptr, ".");
    EmptyOverloadSet = Wide::Memory::MakeUnique<OverloadSet>(std::unordered_set<OverloadResolvable*>(), nullptr, *this);
    ClangInclude = Wide::Memory::MakeUnique<ClangIncludeEntity>(*this);
    Void = Wide::Memory::MakeUnique<VoidType>(*this);
    Boolean = Wide::Memory::MakeUnique<Bool>(*this);
    null = Wide::Memory::MakeUnique<NullType>(*this);
    PointerCast = Wide::Memory::MakeUnique<PointerCastType>();
    Move = Wide::Memory::MakeUnique<MoveType>();

    auto context = Context{ global, Lexer::Range(std::make_shared<std::string>("Analyzer internal.")) };
    global->AddSpecialMember("cpp", ClangInclude->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("void", GetConstructorType(Void.get())->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("global", global->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("int8", GetConstructorType(GetIntegralType(8, true))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("uint8", GetConstructorType(GetIntegralType(8, false))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("int16", GetConstructorType(GetIntegralType(16, true))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("uint16", GetConstructorType(GetIntegralType(16, false))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("int32", GetConstructorType(GetIntegralType(32, true))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("uint32", GetConstructorType(GetIntegralType(32, false))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("int64", GetConstructorType(GetIntegralType(64, true))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("uint64", GetConstructorType(GetIntegralType(64, false))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("float32", GetConstructorType(GetFloatType(32))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("float64", GetConstructorType(GetFloatType(64))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("bool", GetConstructorType(Boolean.get())->BuildValueConstruction(Expression::NoInstance(), {}, context));

    global->AddSpecialMember("byte", GetConstructorType(GetIntegralType(8, false))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("int", GetConstructorType(GetIntegralType(32, true))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("short", GetConstructorType(GetIntegralType(16, true))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("long", GetConstructorType(GetIntegralType(64, true))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("float", GetConstructorType(GetFloatType(32))->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("double", GetConstructorType(GetFloatType(64))->BuildValueConstruction(Expression::NoInstance(), {}, context));

    global->AddSpecialMember("null", GetNullType()->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("reinterpret_cast", GetOverloadSet(PointerCast.get())->BuildValueConstruction(Expression::NoInstance(), {}, context));
    global->AddSpecialMember("move", GetOverloadSet(Move.get())->BuildValueConstruction(Expression::NoInstance(), {}, context));

    Module::AddDefaultHandlers(*this);
    Expression::AddDefaultHandlers(*this);
    FunctionSkeleton::AddDefaultHandlers(*this);
    AddDefaultContextHandlers(*this);
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
    // Ensure that all layout conversions are done before codegen
    for (auto&& udt : UDTs)
        for (auto&& pair : udt.second)
            pair.second->size();
    if (AggregateTU)
        AggregateTU->GenerateCodeAndLinkModule(ConstantModule.get(), layout, *this);
    for (auto&& tu : headers)
        tu.second.GenerateCodeAndLinkModule(ConstantModule.get(), layout, *this);

    for (auto&& set : WideFunctions)
        for (auto&& signature : set.second)
            signature.second->EmitCode(ConstantModule.get());
    for (auto&& pair : ExportedTypes)
        pair.first->Export(ConstantModule.get());
    std::string err;
	auto copy = std::unique_ptr<llvm::Module>(llvm::CloneModule(ConstantModule.get()));
    llvm::DiagnosticInfo* info;
    if (llvm::Linker::LinkModules(module, copy.get(), [&](const llvm::DiagnosticInfo& info) {
        llvm::raw_string_ostream stream(err);
        llvm::DiagnosticPrinterRawOStream printer(stream);
        info.print(printer);
        stream.flush();
    }))
        throw std::runtime_error("Internal compiler error: LLVM Linking failed\n" + err);    
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
    if (t->isFunctionType()) {
        auto funty = llvm::cast<const clang::FunctionProtoType>(t.getTypePtr());
        return GetFunctionType(funty, from);
    }
    if (t->isMemberDataPointerType()) {
        auto memptrty = llvm::cast<const clang::MemberPointerType>(t.getTypePtr());
        auto source = GetClangType(from, from.GetASTContext().getRecordType(memptrty->getClass()->getAsCXXRecordDecl()));
        auto dest = GetClangType(from, memptrty->getPointeeType());
        return GetMemberDataPointer(source, dest);
    }
    if (t->isMemberFunctionPointerType()) {
        auto memfuncty = llvm::cast<const clang::MemberPointerType>(t.getTypePtr());
        auto source = GetClangType(from, from.GetASTContext().getRecordType(memfuncty->getClass()->getAsCXXRecordDecl()));
        auto dest = dynamic_cast<FunctionType*>(GetClangType(from, memfuncty->getPointeeType()));
        assert(dest);
        return GetMemberFunctionPointer(source, dest);
    }
    if (t->isConstantArrayType()) {
        auto arrty = llvm::cast<const clang::ConstantArrayType>(t.getTypePtr());
        auto elemty = GetClangType(from, arrty->getElementType());
        auto size = arrty->getSize();
        return GetArrayType(elemty, size.getLimitedValue());
    }
    if (auto recdecl = t->getAsCXXRecordDecl())
        if (GeneratedClangTypes.find(recdecl) != GeneratedClangTypes.end())
            return GeneratedClangTypes[recdecl].ty;
    if (ClangTypes.find(t) == ClangTypes.end())
        ClangTypes[t] = Wide::Memory::MakeUnique<ClangType>(&from, t, *this);
    return ClangTypes[t].get();
}
void Analyzer::AddClangType(const clang::CXXRecordDecl* t, ClangTypeInfo match) {
    assert(GeneratedClangTypes.find(t) == GeneratedClangTypes.end());
    GeneratedClangTypes[t] = match;
}

ClangNamespace* Analyzer::GetClangNamespace(ClangTU& tu, clang::DeclContext* con) {
    assert(con);
    if (ClangNamespaces.find(con) == ClangNamespaces.end())
        ClangNamespaces[con] = Wide::Memory::MakeUnique<ClangNamespace>(con, &tu, *this);
    return ClangNamespaces[con].get();
}

WideFunctionType* Analyzer::GetFunctionType(Type* ret, const std::vector<Type*>& t, bool variadic, llvm::CallingConv::ID conv) {
    if (FunctionTypes.find(ret) == FunctionTypes.end()
     || FunctionTypes[ret].find(t) == FunctionTypes[ret].end()
     || FunctionTypes[ret][t].find(conv) == FunctionTypes[ret][t].end()
     || FunctionTypes[ret][t][conv].find(variadic) == FunctionTypes[ret][t][conv].end())
        FunctionTypes[ret][t][conv][variadic] = Wide::Memory::MakeUnique<WideFunctionType>(ret, t, *this, conv, variadic);
    return FunctionTypes[ret][t][conv][variadic].get();
}

Function* Analyzer::GetWideFunction(FunctionSkeleton* skeleton) {
    return GetWideFunction(skeleton, GetFunctionParameters(skeleton->GetASTFunction(), skeleton->GetContext()));
}
Function* Analyzer::GetWideFunction(FunctionSkeleton* skeleton, const std::vector<Type*>& types) {
    if (WideFunctions.find(skeleton) == WideFunctions.end()
        || WideFunctions[skeleton].find(types) == WideFunctions[skeleton].end())
        WideFunctions[skeleton][types] = Wide::Memory::MakeUnique<Function>(*this, skeleton, types);
    return WideFunctions[skeleton][types].get();
}

Module* Analyzer::GetWideModule(const Parse::Module* p, Module* higher, std::string name) {
    if (WideModules.find(p) == WideModules.end())
        WideModules[p] = Wide::Memory::MakeUnique<Module>(p, higher, name, *this);
    return WideModules[p].get();
}

LvalueType* Analyzer::GetLvalueType(Type* t) {
    assert(t);
    if (t == Void.get())
        assert(false);

    if (LvalueTypes.find(t) == LvalueTypes.end())
        LvalueTypes[t] = Wide::Memory::MakeUnique<LvalueType>(t, *this);
    
    return LvalueTypes[t].get();
}

Type* Analyzer::GetRvalueType(Type* t) {
    assert(t);
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
OverloadSet* Analyzer::GetOverloadSet() {
    return EmptyOverloadSet.get();
}
OverloadSet* Analyzer::GetOverloadSet(OverloadResolvable* c) {
    std::unordered_set<OverloadResolvable*> set;
    set.insert(c);
    return GetOverloadSet(set);
}
OverloadSet* Analyzer::GetOverloadSet(std::unordered_set<OverloadResolvable*> set, Type* nonstatic) {    
    if (nonstatic && !nonstatic->IsReference())
        nonstatic = GetRvalueType(nonstatic);
    if (callable_overload_sets.find(set) != callable_overload_sets.end())
        if (callable_overload_sets[set].find(nonstatic) != callable_overload_sets[set].end())
            return callable_overload_sets[set][nonstatic].get();
    if (nonstatic && (dynamic_cast<UserDefinedType*>(nonstatic->Decay()) || dynamic_cast<ClangType*>(nonstatic->Decay())))
        callable_overload_sets[set][nonstatic] = Wide::Memory::MakeUnique<OverloadSet>(set, nonstatic, *this);
    else
        callable_overload_sets[set][nonstatic] = Wide::Memory::MakeUnique<OverloadSet>(set, nullptr, *this);
    return callable_overload_sets[set][nonstatic].get();
}

UserDefinedType* Analyzer::GetUDT(const Parse::Type* t, Type* context, std::string name) {
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
    assert(to);
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
    if (context && !context->IsReference())
        context = GetRvalueType(context);
    auto pair = std::make_pair(f, s);
    if (CombinedOverloadSets.find(pair) == CombinedOverloadSets.end()
     || CombinedOverloadSets[pair].find(context) == CombinedOverloadSets[pair].end())
        CombinedOverloadSets[pair][context] = Wide::Memory::MakeUnique<OverloadSet>(f, s, *this, context);      
    return CombinedOverloadSets[pair][context].get();
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
    if (context && !context->IsReference())
        context = GetRvalueType(context);
    if (clang_overload_sets.find(decls) == clang_overload_sets.end()
     || clang_overload_sets[decls].find(context) == clang_overload_sets[decls].end()) {
        clang_overload_sets[decls][context] = Wide::Memory::MakeUnique<OverloadSet>(decls, from, context, *this);
    }
    return clang_overload_sets[decls][context].get();
}
std::vector<Type*> Analyzer::GetFunctionParameters(const Parse::FunctionBase* func, Type* context) {
    std::vector<Type*> out;
    if (HasImplicitThis(func, context)) {
        // If we're exported as an rvalue-qualified function, we need rvalue.
        if (auto astfun = dynamic_cast<const Parse::AttributeFunctionBase*>(func)) {
            for (auto&& attr : astfun->attributes) {
                if (auto name = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
                    if (auto string = boost::get<std::string>(&name->val)) {
                        if (*string == "export") {
                            auto expr = context->analyzer.AnalyzeExpression(context, attr.initializer.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
                            auto overset = dynamic_cast<OverloadSet*>(expr->GetType(Expression::NoInstance())->Decay());
                            if (!overset)
                                throw SpecificError<ExportNonOverloadSet>(*this, attr.initializer->location, "Attempted to export as a non-overload set.");
                            auto tuanddecl = overset->GetSingleFunction();
                            if (!tuanddecl.second) throw SpecificError<ExportNotSingleFunction>(*this, attr.initializer->location, "The overload set was not a single C++ function.");
                            auto tu = tuanddecl.first;
                            auto decl = tuanddecl.second;
                            if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                                if (!meth->isStatic()) {
                                    if (meth->getType()->getAs<clang::FunctionProtoType>()->getExtProtoInfo().RefQualifier == clang::RefQualifierKind::RQ_RValue)
                                        out.push_back(GetRvalueType(GetNonstaticContext(func, context)));
                                    else
                                        out.push_back(GetLvalueType(GetNonstaticContext(func, context)));
                                }
                            }
                        }
                    }
                }
            }
        }
        if (out.empty())
            out.push_back(GetLvalueType(GetNonstaticContext(func, context)));
    }
    for (auto&& arg : func->args) {
        if (!arg.type) {
            if (arg.default_value) {
                auto p_type = AnalyzeExpression(context, arg.default_value.get(), [](Parse::Name, Lexer::Range) { return nullptr; })->GetType(Expression::NoInstance())->Decay();
                QuickInfo(arg.location, p_type);
                ParameterHighlight(arg.location);
                out.push_back(p_type);
                continue;
            }
            ParameterHighlight(arg.location);
            out.push_back(nullptr);
            continue;
        }      

        auto ty_expr = arg.type.get();
        auto expr = AnalyzeExpression(context, ty_expr, [](Parse::Name, Lexer::Range) { return nullptr; });
        auto p_type = expr->GetType(Expression::NoInstance())->Decay();
        auto con_type = dynamic_cast<ConstructorType*>(p_type);
        if (!con_type)
            throw SpecificError<FunctionArgumentNotType>(*this, arg.type->location, "Function argument type was not a type.");
        if (arg.name == "this") {
            if (&arg == &func->args[0]) {
                if (!GetNonstaticContext(func, context))
                    throw SpecificError<ExplicitThisNoMember>(*this, arg.location, "Explicit this in a non-member function.");
                if (GetNonstaticContext(func, context) != con_type->GetConstructedType()->Decay())
                    throw SpecificError<ExplicitThisDoesntMatchMember>(*this, arg.location, "Explicit this's type did not match member type.");
            } else
                throw SpecificError<ExplicitThisNotFirstArgument>(*this, arg.location, "Explicit this was not the first argument.");
        }
        QuickInfo(arg.location, con_type->GetConstructedType());
        ParameterHighlight(arg.location);
        out.push_back(con_type->GetConstructedType());
    }
    return out;
}
bool Analyzer::HasImplicitThis(const Parse::FunctionBase* func, Type* context) {
    // If we are a member without an explicit this, then we have an implicit this.
    if (!GetNonstaticContext(func, context))
        return false;
    if (func->args.size() > 0) {
        if (func->args[0].name == "this") {
            return false;
        }
    }
    return true;
}
Type* Analyzer::GetNonstaticContext(const Parse::FunctionBase* p, Type* context) {
    if (context->IsNonstaticMemberContext())
        return context;
    // May be exported.
    if (auto astfun = dynamic_cast<const Parse::AttributeFunctionBase*>(p)) {
        for (auto&& attr : astfun->attributes) {
            if (auto name = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
                if (auto string = boost::get<std::string>(&name->val)) {
                    if (*string == "export") {
                        auto expr = context->analyzer.AnalyzeExpression(context, attr.initializer.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
                        auto overset = dynamic_cast<OverloadSet*>(expr->GetType(Expression::NoInstance())->Decay());
                        if (!overset)
                            continue;
                        auto tuanddecl = overset->GetSingleFunction();
                        if (!tuanddecl.second) throw SpecificError<ExportNotSingleFunction>(*this, attr.initializer->location, "The overload set was not a single C++ function.");
                        auto tu = tuanddecl.first;
                        auto decl = tuanddecl.second;
                        if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                            return context->analyzer.GetClangType(*tu, tu->GetASTContext().getRecordType(meth->getParent()));
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}
FunctionSkeleton* Analyzer::GetWideFunction(const Parse::FunctionBase* p, Type* context, std::string name, Type* nonstatic_context, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
    if (nonstatic_context == nullptr)
        return GetWideFunction(p, context, name, std::function<Type*(Expression::InstanceKey)>(), NonstaticLookup);
    return GetWideFunction(p, context, name, [=](Expression::InstanceKey key) { return nonstatic_context; }, NonstaticLookup);
}

OverloadResolvable* Analyzer::GetCallableForFunction(FunctionSkeleton* skel) {
    if (FunctionCallables.find(skel) != FunctionCallables.end())
        return FunctionCallables.at(skel).get();

    struct FunctionCallable : public OverloadResolvable {
        FunctionCallable(FunctionSkeleton* skel)
            : skeleton(skel)
        {}
        
        FunctionSkeleton* skeleton;

        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
            // If we are a member and we have an explicit this then treat the first normally.
            // Else if we are a member, blindly accept whatever is given for argument 0 as long as it's the member type.
            // Else, treat the first argument normally.
            auto context = types.size() > 0 && dynamic_cast<LambdaType*>(types[0]->Decay())
                ? types[0]->Decay()
                : skeleton->GetContext();
            auto parameters = a.GetFunctionParameters(skeleton->GetASTFunction(), context);
            //if (dynamic_cast<const Parse::Lambda*>(skeleton->GetASTFunction()))
            //    if (dynamic_cast<LambdaType*>(types[0]->Decay()))
            //        if (types.size() == parameters.size() + 1)
            //            parameters.insert(parameters.begin(), types[0]);
            if (types.size() != parameters.size()) return Util::none;
            std::vector<Type*> result;
            for (unsigned i = 0; i < types.size(); ++i) {
                if (a.HasImplicitThis(skeleton->GetASTFunction(), context) && i == 0) {
                    // First, if no conversion is necessary.
                    if (Type::IsFirstASecond(types[i], parameters[i], source)) {
                        result.push_back(parameters[i]);
                        continue;
                    }
                    // If the parameter is-a nonstatic-context&&, then we're good. Let Function::AdjustArguments handle the adjustment, if necessary.
                    if (Type::IsFirstASecond(types[i], a.GetRvalueType(a.GetNonstaticContext(skeleton->GetASTFunction(), context)), source)) {
                        result.push_back(parameters[i]);
                        continue;
                    }
                    return Util::none;
                }
                auto parameter = parameters[i] ? parameters[i] : types[i]->Decay();
                if (Type::IsFirstASecond(types[i], parameter, source))
                    result.push_back(parameter);
                else
                    return Util::none;
            }
            return result;
        }

        Callable* GetCallableForResolution(std::vector<Type*> types, Type*, Analyzer& a) override final {
            if (auto function = dynamic_cast<const Parse::Function*>(skeleton->GetASTFunction()))
                if (function->deleted)
                    return nullptr;
            if (auto con = dynamic_cast<const Parse::Constructor*>(skeleton->GetASTFunction()))
                if (con->deleted)
                    return nullptr;
            return a.GetWideFunction(skeleton, types);
        }
    };
    FunctionCallables[skel] = Wide::Memory::MakeUnique<FunctionCallable>(skel);
    return FunctionCallables.at(skel).get();
}

Parse::Access Semantic::GetAccessSpecifier(Type* from, Type* to) {
    auto source = from->Decay();
    auto target = to->Decay();
    if (source == target) return Parse::Access::Private; 
    if (source->IsDerivedFrom(target) == Type::InheritanceRelationship::UnambiguouslyDerived)
        return Parse::Access::Protected;
    if (auto context = source->GetContext())
        return GetAccessSpecifier(context, target);
    return Parse::Access::Public;
}
namespace {
    void ProcessFunction(const Parse::AttributeFunctionBase* f, Analyzer& a, Module* m, std::string name, std::function<void(const Parse::AttributeFunctionBase*, std::string, Module*)> callback) {
        if (IsMultiTyped(f)) return;
        bool exported = false;
        for (auto&& attr : f->attributes) {
            if (auto ident = dynamic_cast<const Parse::Identifier*>(attr.initialized.get()))
                if (auto str = boost::get<std::string>(&ident->val))
                    if (*str == "export")
                        exported = true;
        }
        if (!exported) return;
        if (auto func = dynamic_cast<const Parse::Function*>(f))
            if (func->deleted)
                return;
        if (auto con = dynamic_cast<const Parse::Constructor*>(f))
            if (con->defaulted)
                return;
        callback(f, name, m);
    }
    template<typename T> void ProcessOverloadSet(std::unordered_set<std::shared_ptr<T>> set, Analyzer& a, Module* m, std::string name, std::function<void(const Parse::AttributeFunctionBase*, std::string, Module*)> callback) {
        for (auto&& func : set) {
            ProcessFunction(func.get(), a, m, name, callback);
        }
    }
}

void AnalyzeExportedFunctionsInModule(Analyzer& a, Module* m, std::function<void(const Parse::AttributeFunctionBase*, std::string, Module*)> callback) {
    auto mod = m->GetASTModule();
    ProcessOverloadSet(mod->constructor_decls, a, m, "type", callback);
    ProcessOverloadSet(mod->destructor_decls, a, m, "~type", callback);
    for (auto name : mod->OperatorOverloads) {
        for (auto access : name.second) {
            ProcessOverloadSet(access.second, a, m, GetNameAsString(name.first), callback);
        }
    }
    for (auto&& decl : mod->named_decls) {
        if (auto overset = boost::get<std::unique_ptr<Parse::MultipleAccessContainer>>(&decl.second)) {
            if (auto funcs = dynamic_cast<Parse::ModuleOverloadSet<Parse::Function>*>(overset->get()))
                for (auto access : funcs->funcs)
                    ProcessOverloadSet(access.second, a, m, decl.first, callback);
        }
    }
}
void Semantic::AnalyzeExportedFunctions(Analyzer& a, std::function<void(const Parse::AttributeFunctionBase*, std::string, Module*)> callback) {
    AnalyzeExportedFunctionsInModule(a, a.GetGlobalModule(), callback);
}
void Semantic::AnalyzeExportedFunctions(Analyzer& a) {
    AnalyzeExportedFunctions(a, [](const Parse::AttributeFunctionBase* func, std::string name, Module* m) {
        auto skeleton = m->analyzer.GetWideFunction(func, m, name, nullptr, [](Parse::Name, Lexer::Range) { return nullptr; });
        auto function = m->analyzer.GetWideFunction(skeleton);
        function->ComputeBody();
    });
}
OverloadResolvable* Analyzer::GetCallableForTemplateType(const Parse::TemplateType* t, Type* context) {
    if (TemplateTypeCallables.find(t) != TemplateTypeCallables.end())
        return TemplateTypeCallables[t].get();

    struct TemplateTypeCallable : Callable {
        TemplateTypeCallable(Type* con, const Wide::Parse::TemplateType* tempty, std::vector<Type*> args)
        : context(con), templatetype(tempty), types(args) {}
        Type* context;
        const Wide::Parse::TemplateType* templatetype;
        std::vector<Type*> types;
        std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final { return args; }
        std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            return context->analyzer.GetConstructorType(context->analyzer.GetTemplateType(templatetype, context, types, ""))->BuildValueConstruction(key, {}, c);
        }
    };

    struct TemplateTypeResolvable : OverloadResolvable {
        TemplateTypeResolvable(const Parse::TemplateType* f, Type* con)
        : templatetype(f), context(con) {}
        Type* context;
        const Wide::Parse::TemplateType* templatetype;
        std::unordered_map<std::vector<Type*>, std::unique_ptr<TemplateTypeCallable>, VectorTypeHasher> Callables;

        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final {
            if (types.size() != templatetype->arguments.size()) return Util::none;
            std::vector<Type*> valid;
            for (unsigned num = 0; num < types.size(); ++num) {
                auto arg = types[num]->Decay();
                if (!arg->IsConstant())
                    return Util::none;
                if (!templatetype->arguments[num].type) {
                    a.ParameterHighlight(templatetype->arguments[num].location); 
                    valid.push_back(arg);
                    continue;
                }
                auto p_type = a.AnalyzeExpression(context, templatetype->arguments[num].type.get(), [](Parse::Name, Lexer::Range) { return nullptr; })->GetType(Expression::NoInstance())->Decay();
                auto con_type = dynamic_cast<ConstructorType*>(p_type);
                if (!con_type)
                    throw SpecificError<TemplateArgumentNotAType>(a, templatetype->arguments[num].type->location, "Template argument type was not a type.");
                a.QuickInfo(templatetype->arguments[num].location, con_type->GetConstructedType());
                a.ParameterHighlight(templatetype->arguments[num].location);
                if (Type::IsFirstASecond(arg, con_type->GetConstructedType(), source))
                    valid.push_back(con_type->GetConstructedType());
                else
                    return Util::none;
            }
            return valid;
        }
        Callable* GetCallableForResolution(std::vector<Type*> types, Type*, Analyzer& a) override final {
            if (Callables.find(types) != Callables.end())
                return Callables[types].get();
            Callables[types] = Wide::Memory::MakeUnique<TemplateTypeCallable>(context, templatetype, types);
            return Callables[types].get();
        }
    };

    TemplateTypeCallables[t] = Wide::Memory::MakeUnique<TemplateTypeResolvable>(t, context);
    return TemplateTypeCallables[t].get();
}

TemplateType* Analyzer::GetTemplateType(const Wide::Parse::TemplateType* ty, Type* context, std::vector<Type*> arguments, std::string name) {
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
        
        WideTemplateInstantiations[ty][arguments] = Wide::Memory::MakeUnique<TemplateType>(ty->t.get(), *this, context, args, name);
    }
    return WideTemplateInstantiations[ty][arguments].get();
}

Type* Analyzer::GetLiteralStringType() {
    if (!LiteralStringType)
        LiteralStringType = Wide::Memory::MakeUnique<StringType>(*this);
    return LiteralStringType.get();
}
LambdaType* Analyzer::GetLambdaType(FunctionSkeleton* skel, std::vector<std::pair<Parse::Name, Type*>> types) {
    if (LambdaTypes.find(skel) == LambdaTypes.end()
     || LambdaTypes[skel].find(types) == LambdaTypes[skel].end())
        LambdaTypes[skel][types] = Wide::Memory::MakeUnique<LambdaType>(types, skel, *this);
    return LambdaTypes[skel][types].get();
}
bool Semantic::IsMultiTyped(const Parse::FunctionArgument& f) {
    return !f.type;
}
bool Semantic::IsMultiTyped(const Parse::FunctionBase* f) {
    bool ret = false;
    for (auto&& var : f->args)
        ret = ret || IsMultiTyped(var);
    return ret;
}
std::shared_ptr<Expression> Analyzer::AnalyzeExpression(Type* lookup, const Parse::Expression* e, Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
    return AnalyzeExpression(lookup, e, [=](Parse::Name name, Lexer::Range where) {
        if (auto local = current->LookupLocal(GetNameAsString(name)))
            return local;
        return NonstaticLookup(name, where);
    });
}
std::shared_ptr<Expression> Analyzer::AnalyzeExpression(Type* lookup, const Parse::Expression* e, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
    static_assert(std::is_polymorphic<Parse::Expression>::value, "Expression must be polymorphic.");
    auto&& type_info = typeid(*e);
    if (ExpressionCache.find(e) == ExpressionCache.end()
        || ExpressionCache[e].find(lookup) == ExpressionCache[e].end())
        if (ExpressionHandlers.find(type_info) != ExpressionHandlers.end())
            ExpressionCache[e][lookup] = ExpressionHandlers[type_info](e, *this, lookup, NonstaticLookup);
        else
            assert(false && "Attempted to analyze expression for which there was no handler.");
    return ExpressionCache[e][lookup];
}
ClangTU* Analyzer::GetAggregateTU() {
    if (!AggregateTU)
        AggregateCPPHeader("typeinfo", Lexer::Range(std::make_shared<std::string>("Analyzer internal include.")));
    return AggregateTU.get();
}

ArrayType* Analyzer::GetArrayType(Type* t, unsigned num) {
    if (ArrayTypes.find(t) == ArrayTypes.end()
        || ArrayTypes[t].find(num) == ArrayTypes[t].end())
        ArrayTypes[t][num] = Wide::Memory::MakeUnique<ArrayType>(*this, t, num);
    return ArrayTypes[t][num].get();
}
MemberDataPointer* Analyzer::GetMemberDataPointer(Type* source, Type* dest) {
    if (MemberDataPointers.find(source) == MemberDataPointers.end()
     || MemberDataPointers[source].find(dest) == MemberDataPointers[source].end())
        MemberDataPointers[source][dest] = Wide::Memory::MakeUnique<MemberDataPointer>(*this, source, dest);
    return MemberDataPointers[source][dest].get();
}
MemberFunctionPointer* Analyzer::GetMemberFunctionPointer(Type* source, FunctionType* dest) {
    if (MemberFunctionPointers.find(source) == MemberFunctionPointers.end()
     || MemberFunctionPointers[source].find(dest) == MemberFunctionPointers[source].end())
        MemberFunctionPointers[source][dest] = Wide::Memory::MakeUnique<MemberFunctionPointer>(*this, source, dest);
    return MemberFunctionPointers[source][dest].get();
}
WideFunctionType* Analyzer::GetFunctionType(Type* ret, const std::vector<Type*>& t, bool variadic) {
    return GetFunctionType(ret, t, variadic, llvm::CallingConv::C);
}
std::string Semantic::GetOperatorName(Parse::OperatorName name) {
    std::string result = "operator";
    for (auto op : name)
        result += *op;
    return result;
}
std::string Semantic::GetNameAsString(Parse::Name name) {
    if (auto string = boost::get<std::string>(&name))
        return *string;
    return GetOperatorName(boost::get<Parse::OperatorName>(name));
}
std::string Analyzer::GetUniqueFunctionName() {
    return boost::uuids::to_string(uuid_generator());
}
ClangFunctionType* Analyzer::GetFunctionType(const clang::FunctionProtoType* ptr, clang::QualType self, ClangTU& tu) {
    if (ClangMemberFunctionTypes.find(ptr) == ClangMemberFunctionTypes.end()
     || ClangMemberFunctionTypes[ptr].find(self) == ClangMemberFunctionTypes[ptr].end()
     || ClangMemberFunctionTypes[ptr][self].find(&tu) == ClangMemberFunctionTypes[ptr][self].end())
        ClangMemberFunctionTypes[ptr][self][&tu] = Wide::Memory::MakeUnique<ClangFunctionType>(*this, ptr, &tu, self);
    return ClangMemberFunctionTypes[ptr][self][&tu].get();
}
ClangFunctionType* Analyzer::GetFunctionType(const clang::FunctionProtoType* ptr, ClangTU& tu) {
    if (ClangFunctionTypes.find(ptr) == ClangFunctionTypes.end()
     || ClangFunctionTypes[ptr].find(&tu) == ClangFunctionTypes[ptr].end())
        ClangFunctionTypes[ptr][&tu] = Wide::Memory::MakeUnique<ClangFunctionType>(*this, ptr, &tu, Wide::Util::none);
    return ClangFunctionTypes[ptr][&tu].get();
}
ClangFunctionType* Analyzer::GetFunctionType(const clang::FunctionProtoType* ptr, Wide::Util::optional<clang::QualType> self, ClangTU& tu) {
    if (self) return GetFunctionType(ptr, *self, tu);
    return GetFunctionType(ptr, tu);
}
Type* Semantic::CollapseType(Type* source, Type* member) {
    if (!source->IsReference())
        return member;
    if (IsLvalueType(source))
        return source->analyzer.GetLvalueType(member->Decay());

    // It's not a value or an lvalue so must be rvalue.
    return source->analyzer.GetRvalueType(member);
}
llvm::Value* Semantic::CollapseMember(Type* source, std::pair<llvm::Value*, Type*> member, CodegenContext& con) {
    if ((source->IsReference() && member.second->IsReference()) || (source->AlwaysKeepInMemory(con) && !member.second->AlwaysKeepInMemory(con)))
        return con->CreateLoad(member.first);
    return member.first;
}
std::function<void(CodegenContext&)> Semantic::ThrowObject(Expression::InstanceKey key, std::shared_ptr<Expression> expr, Context c) {
    // http://mentorembedded.github.io/cxx-abi/abi-eh.html
    // 2.4.2
    auto ty = expr->GetType(key)->Decay();
    auto RTTI = ty->GetRTTI();
    auto destructor_func = ty->GetDestructorFunction();
    return [=](CodegenContext& con) {
        auto memty = ty->analyzer.GetLvalueType(ty);
        auto destructor = std::make_shared<std::list<std::pair<std::function<void(CodegenContext&)>, bool>>::iterator>();
        auto except_memory = CreatePrimGlobal(Range::Elements(expr), memty, [=](CodegenContext& con) {
            auto except_memory = con->CreateCall(con.GetCXAAllocateException(), { llvm::ConstantInt::get(con.GetPointerSizedIntegerType(), ty->size(), false) });
            *destructor = con.AddDestructor([except_memory](CodegenContext& con) {
                auto free_exception = con.GetCXAFreeException();
                con->CreateCall(free_exception, { except_memory });
            });
            return con->CreatePointerCast(except_memory, memty->GetLLVMType(con));
        });
        // There is no longer a guarantee thas, as an argument, except_memory will be in the same CodegenContext
        // and the iterator could be invalidated. Strictly get the value in the original CodegenContext that ThrowStatement::GenerateCode
        // is called with so that we can erase the destructor later.
        auto exception = BuildChain(BuildChain(except_memory, Type::BuildInplaceConstruction(con.func, except_memory, { std::move(expr) }, c)), except_memory);
        auto value = exception->GetValue(con);
        auto cxa_throw = con.GetCXAThrow();
        // Throw this shit.
        // If we got here then creating the exception value didn't throw. Don't destroy it now.
        con.EraseDestructor(*destructor);
        llvm::Value* llvmdestructor;
        if (ty->IsTriviallyDestructible()) {
            llvmdestructor = llvm::Constant::getNullValue(con.GetInt8PtrTy());
        } else
            llvmdestructor = con->CreatePointerCast(destructor_func(con), con.GetInt8PtrTy());
        llvm::Value* args[] = { con->CreatePointerCast(value, con.GetInt8PtrTy()), con->CreatePointerCast(RTTI(con), con.GetInt8PtrTy()), llvmdestructor };
        // Do we have an existing handler to go to? If we do, then first land, then branch directly to it.
        // Else, kill everything and GTFO this function and let the EH routines worry about it.
        if (con.HasDestructors() || con.EHHandler)
            con->CreateInvoke(cxa_throw, con.GetUnreachableBlock(), con.CreateLandingpadForEH(), args);
        else {
            con->CreateCall(cxa_throw, args);
            // This is unreachable, but terminate the block so we know to stop code-generating.
            con->CreateUnreachable();
        }
    };
}
ClangTypeInfo* Analyzer::MaybeGetClangTypeInfo(const clang::CXXRecordDecl* decl) {
    if (GeneratedClangTypes.find(decl) != GeneratedClangTypes.end())
        return &GeneratedClangTypes[decl];
    return nullptr;
}
std::string Analyzer::GetTypeExport(Type* t) {
    if (ExportedTypes.find(t) == ExportedTypes.end()) {
        // Break any recursion here.
        ExportedTypes[t] = "";
        ExportedTypes[t] = t->GetExportBody();
    }
    return t->Export();
}
std::string Analyzer::GetTypeExports() {
    std::string exports;
    for (auto&& pair : ExportedTypes)
        exports += pair.second;
    return exports;
}
FunctionSkeleton* Analyzer::GetWideFunction(const Parse::FunctionBase* p, Type* context, std::string name, std::function<Type*(Expression::InstanceKey)> nonstatic_context, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
    if (FunctionSkeletons.find(p) == FunctionSkeletons.end()
     || FunctionSkeletons[p].find(context) == FunctionSkeletons[p].end())
        FunctionSkeletons[p][context] = Wide::Memory::MakeUnique<FunctionSkeleton>(p, *this, context, name, nonstatic_context, NonstaticLookup);
    return FunctionSkeletons[p][context].get();
}

llvm::APInt Analyzer::EvaluateConstantIntegerExpression(std::shared_ptr<Expression> e, Expression::InstanceKey key) {
    assert(dynamic_cast<IntegralType*>(e->GetType(key)));
    assert(e->IsConstant(key));
    if (auto integer = dynamic_cast<Integer*>(e.get()))
        return integer->value;
    // BUG: Invoking MCJIT causes second MCJIT invocation to fail. This causes spurious test failures.
    auto evalfunc = llvm::Function::Create(llvm::FunctionType::get(e->GetType(key)->GetLLVMType(ConstantModule.get()), {}, false), llvm::GlobalValue::LinkageTypes::InternalLinkage, GetUniqueFunctionName(), ConstantModule.get());
    CodegenContext::EmitFunctionBody(evalfunc, {}, [e](CodegenContext& con) {
        con->CreateRet(e->GetValue(con));
    });
	auto mod = Wide::Util::CreateModuleForTriple(ConstantModule->getTargetTriple(), ConstantModule->getContext());
	evalfunc = Wide::Util::CloneFunctionIntoModule(evalfunc, mod.get());
    llvm::EngineBuilder b(std::move(mod));
    b.setEngineKind(llvm::EngineKind::JIT);
    std::unique_ptr<llvm::ExecutionEngine> ee(b.create());
    ee->finalizeObject();
    auto result = ee->runFunction(evalfunc, std::vector<llvm::GenericValue>());
    evalfunc->eraseFromParent();
    return result.IntVal;
}
std::shared_ptr<Statement> Semantic::AnalyzeStatement(Analyzer& analyzer, FunctionSkeleton* skel, const Parse::Statement* s, Type* parent, Scope* current, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> nonstatic) {
    auto local_nonstatic = [=](Parse::Name name, Lexer::Range where) { 
        if (auto result = current->LookupLocal(Semantic::GetNameAsString(name)))
            return result;
        return nonstatic(name, where);
    };
    if (analyzer.ExpressionHandlers.find(typeid(*s)) != analyzer.ExpressionHandlers.end())
        return analyzer.ExpressionHandlers[typeid(*s)](static_cast<const Parse::Expression*>(s), analyzer, parent, local_nonstatic);
    return analyzer.StatementHandlers[typeid(*s)](s, skel, analyzer, parent, current, local_nonstatic);
}
std::shared_ptr<Expression> Semantic::LookupFromImport(Type* context, Wide::Parse::Name name, Lexer::Range where, Parse::Import* imp) {
    auto propagate = [=]() -> std::shared_ptr<Expression> {
        if (imp->previous) return LookupFromImport(context, name, where, imp->previous.get());
        else return nullptr;
    };
    if (std::find(imp->hidden.begin(), imp->hidden.end(), name) != imp->hidden.end())
        return propagate();
    if (imp->names.size() != 0)
        if (std::find(imp->names.begin(), imp->names.end(), name) == imp->names.end())
            return propagate();
    auto con = context->analyzer.AnalyzeExpression(context->analyzer.GetGlobalModule(), imp->from.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
    if (auto result = Type::AccessMember(Expression::NoInstance(), con, name, { context, where })) {
        auto subresult = propagate();
        if (!subresult) return result;
        auto over1 = dynamic_cast<OverloadSet*>(result->GetType(Expression::NoInstance()));
        auto over2 = dynamic_cast<OverloadSet*>(subresult->GetType(Expression::NoInstance()));
        if (over1 && over2)
            return context->analyzer.GetOverloadSet(over1, over2)->BuildValueConstruction(Expression::NoInstance(), {}, { context, where });
        throw SpecificError<ImportIdentifierLookupAmbiguous>(context->analyzer, where, "Ambiguous lookup of name " + Semantic::GetNameAsString(name));
    }
    return propagate();
}
std::shared_ptr<Expression> Semantic::LookupFromContext(Type* context, Parse::Name name, Lexer::Range where) {
    if (!context) return nullptr;
    context = context->Decay();
    if (context->analyzer.ContextLookupHandlers.find(typeid(*context)) != context->analyzer.ContextLookupHandlers.end()) {
        auto result = context->analyzer.ContextLookupHandlers.at(typeid(*context))(context, name, where);
        if (result)
            return result;
        return LookupFromContext(context->GetContext(), name, where);
    }
    if (auto result = Type::AccessMember(Expression::NoInstance(), context->BuildValueConstruction(Expression::NoInstance(), {}, { context, where }), name, { context, where }))
        return result;
    return LookupFromContext(context->GetContext(), name, where);

    if (auto udt = dynamic_cast<UserDefinedType*>(context))
        return LookupFromContext(context, name, where);
    if (auto self = Type::AccessMember(Expression::NoInstance(), context->BuildValueConstruction(Expression::NoInstance(), {}, { context, where }), "this", { context, where })) {
        if (auto result = Type::AccessMember(Expression::NoInstance(), self, name, { context, where }))
            return result;
    }
    return LookupFromContext(context, name, where);
}
std::shared_ptr<Expression> Semantic::LookupIdentifier(Type* context, Parse::Name name, Lexer::Range where, Parse::Import* imports, std::function<std::shared_ptr<Expression>(Parse::Name, Lexer::Range)> NonstaticLookup) {
    if (auto result = NonstaticLookup(name, where))
        return result;
    // No non-static results found. Unify the results from context and from imports.
    if (!context) return nullptr;
    context = context->Decay();
    auto result = LookupFromContext(context, name, where);
    auto result2 = imports ? LookupFromImport(context, name, where, imports) : nullptr;
    if (!result) return result2;
    if (!result2) return result;
    auto over1 = dynamic_cast<OverloadSet*>(result->GetType(Expression::NoInstance()));
    auto over2 = dynamic_cast<OverloadSet*>(result2->GetType(Expression::NoInstance()));
    if (over1 && over2)
        return context->analyzer.GetOverloadSet(over1, over2)->BuildValueConstruction(Expression::NoInstance(), {}, { context, where });
    throw SpecificError<IdentifierLookupAmbiguous>(context->analyzer, where, "Ambiguous lookup of name " + Semantic::GetNameAsString(name));
}
void Semantic::AddDefaultContextHandlers(Analyzer& a) {
    AddHandler<Semantic::Module>(a.ContextLookupHandlers, [](Module* mod, Parse::Name name, Lexer::Range where) {
        auto local_mod_instance = mod->BuildValueConstruction(Expression::NoInstance(), {}, Context{ mod, where });
        if (auto string = boost::get<std::string>(&name))
            return mod->AccessNamedMember(Expression::NoInstance(), local_mod_instance, *string, { mod, where });
        auto set = mod->AccessMember(boost::get<Parse::OperatorName>(name), GetAccessSpecifier(mod, mod), OperatorAccess::Explicit);
        if (mod->IsLookupContext())
            return mod->analyzer.GetOverloadSet(set, mod->analyzer.GetOverloadSet())->BuildValueConstruction(Expression::NoInstance(), {}, { mod, where });
        return mod->analyzer.GetOverloadSet(set, mod->analyzer.GetOverloadSet(), mod)->BuildValueConstruction(Expression::NoInstance(), { local_mod_instance }, { mod, where });
    });

    AddHandler<Semantic::UserDefinedType>(a.ContextLookupHandlers, [](UserDefinedType* udt, Parse::Name name, Lexer::Range where) -> std::shared_ptr<Expression> {
        if (auto nam = boost::get<std::string>(&name))
            return udt->AccessStaticMember(*nam, { udt, where });
        return nullptr;
    });

    AddHandler<Semantic::TemplateType>(a.ContextLookupHandlers, [](TemplateType* udt, Parse::Name name, Lexer::Range where) -> std::shared_ptr<Expression> {
        return udt->analyzer.ContextLookupHandlers[typeid(UserDefinedType)](udt, name, where);
    });

    AddHandler<Semantic::LambdaType>(a.ContextLookupHandlers, [](LambdaType* lambda, Parse::Name, Lexer::Range where) -> std::shared_ptr < Expression > {
        return nullptr;
    });
}