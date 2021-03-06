#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/StringType.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Functions/Function.h>
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
#include <unordered_set>
#include <fstream>
#include <boost/uuid/uuid_io.hpp>
#include <Wide/Util/Codegen/GetMCJITProcessTriple.h>
#include <Wide/Semantic/Functions/FunctionSkeleton.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <Wide/Util/Codegen/CreateModule.h>
#include <Wide/Util/Codegen/CloneFunctionIntoModule.h>
#include <Wide/Util/Codegen/CloneModule.h>
#include <Wide/Semantic/Functions/UserDefinedDestructor.h>
#include <Wide/Semantic/Functions/UserDefinedConstructor.h>
#include <Wide/Semantic/Functions/DefaultedConstructor.h>
#include <Wide/Semantic/Functions/DefaultedAssignmentOperator.h>

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
#include <llvm/IR/Verifier.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;
using namespace Functions;

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

Analyzer::Analyzer(const Options::Clang& opts, const Parse::Module* GlobalModule, llvm::LLVMContext& con)
    : clangopts(&opts)
    , layout(::GetDataLayout(opts.TargetOptions.Triple))
    , ConstantModule(Wide::Util::CreateModuleForTriple(Wide::Util::GetMCJITProcessTriple(), con))
{
    assert(opts.LanguageOptions.CXXExceptions);
    assert(opts.LanguageOptions.RTTI);
    struct PointerCastType : OverloadResolvable, Callable {
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Location source) override final {
            if (types.size() != 2) return Util::none;
            auto conty = dynamic_cast<ConstructorType*>(types[0]->Decay());
            if (!conty) return Util::none;
            if (!dynamic_cast<PointerType*>(conty->GetConstructedType())) return Util::none;
            if (!dynamic_cast<PointerType*>(types[1]->Decay())) return Util::none;
            return types;
        }
        Callable* GetCallableForResolution(std::vector<Type*>, Location, Analyzer& a) override final { return this; }
        std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            auto conty = dynamic_cast<ConstructorType*>(args[0]->GetType()->Decay());
            return CreatePrimGlobal(Range::Container(args), conty->GetConstructedType(), [=](CodegenContext& con) {
                args[0]->GetValue(con);
                auto val = args[1]->GetValue(con);
                return con->CreatePointerCast(val, conty->GetConstructedType()->GetLLVMType(con));
            });
        }
        std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            return args; 
        }
    };

    struct MoveType : OverloadResolvable, Callable {
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Location source) override final {
            if (types.size() == 1) return types; 
            return Util::none; 
        }
        Callable* GetCallableForResolution(std::vector<Type*>, Location, Analyzer& a) override final {
            return this; 
        }
        std::vector<std::shared_ptr<Expression>> AdjustArguments(std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            return args; 
        }
        std::shared_ptr<Expression> CallFunction(std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            return Wide::Memory::MakeUnique<RvalueCast>(std::move(args[0]));
        }
    };

    global = GetWideModule(GlobalModule, Location::Empty, ".");
    EmptyOverloadSet = Wide::Memory::MakeUnique<OverloadSet>(std::unordered_set<OverloadResolvable*>(), nullptr, *this);
    ClangInclude = Wide::Memory::MakeUnique<ClangIncludeEntity>(*this);
    Void = Wide::Memory::MakeUnique<VoidType>(*this);
    Boolean = Wide::Memory::MakeUnique<Bool>(*this);
    null = Wide::Memory::MakeUnique<NullType>(*this);
    PointerCast = Wide::Memory::MakeUnique<PointerCastType>();
    Move = Wide::Memory::MakeUnique<MoveType>();

    auto context = Context{ Location(*this), Lexer::Range(std::make_shared<std::string>("Analyzer internal.")) };
    global->AddSpecialMember("cpp", ClangInclude->BuildValueConstruction({}, context));
    global->AddSpecialMember("void", GetConstructorType(Void.get())->BuildValueConstruction({}, context));
    global->AddSpecialMember("global", global->BuildValueConstruction({}, context));
    global->AddSpecialMember("int8", GetConstructorType(GetIntegralType(8, true))->BuildValueConstruction({}, context));
    global->AddSpecialMember("uint8", GetConstructorType(GetIntegralType(8, false))->BuildValueConstruction({}, context));
    global->AddSpecialMember("int16", GetConstructorType(GetIntegralType(16, true))->BuildValueConstruction({}, context));
    global->AddSpecialMember("uint16", GetConstructorType(GetIntegralType(16, false))->BuildValueConstruction({}, context));
    global->AddSpecialMember("int32", GetConstructorType(GetIntegralType(32, true))->BuildValueConstruction({}, context));
    global->AddSpecialMember("uint32", GetConstructorType(GetIntegralType(32, false))->BuildValueConstruction({}, context));
    global->AddSpecialMember("int64", GetConstructorType(GetIntegralType(64, true))->BuildValueConstruction({}, context));
    global->AddSpecialMember("uint64", GetConstructorType(GetIntegralType(64, false))->BuildValueConstruction({}, context));
    global->AddSpecialMember("float32", GetConstructorType(GetFloatType(32))->BuildValueConstruction({}, context));
    global->AddSpecialMember("float64", GetConstructorType(GetFloatType(64))->BuildValueConstruction({}, context));
    global->AddSpecialMember("bool", GetConstructorType(Boolean.get())->BuildValueConstruction({}, context));

    global->AddSpecialMember("byte", GetConstructorType(GetIntegralType(8, false))->BuildValueConstruction({}, context));
    global->AddSpecialMember("int", GetConstructorType(GetIntegralType(32, true))->BuildValueConstruction({}, context));
    global->AddSpecialMember("short", GetConstructorType(GetIntegralType(16, true))->BuildValueConstruction({}, context));
    global->AddSpecialMember("long", GetConstructorType(GetIntegralType(64, true))->BuildValueConstruction({}, context));
    global->AddSpecialMember("float", GetConstructorType(GetFloatType(32))->BuildValueConstruction({}, context));
    global->AddSpecialMember("double", GetConstructorType(GetFloatType(64))->BuildValueConstruction({}, context));

    global->AddSpecialMember("null", GetNullType()->BuildValueConstruction({}, context));
    global->AddSpecialMember("reinterpret_cast", GetOverloadSet(PointerCast.get())->BuildValueConstruction({}, context));
    global->AddSpecialMember("move", GetOverloadSet(Move.get())->BuildValueConstruction({}, context));

    Module::AddDefaultHandlers(*this);
    Expression::AddDefaultHandlers(*this);
    FunctionSkeleton::AddDefaultHandlers(*this);
    LambdaType::AddDefaultHandlers(*this);
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
        AggregateTU->GenerateCodeAndLinkModule(ConstantModule.get(), layout, *this);
    for (auto&& tu : headers)
        tu.second.GenerateCodeAndLinkModule(ConstantModule.get(), layout, *this);

    for (auto&& set : WideFunctions)
        for (auto&& signature : set.second)
            signature.second->EmitCode(ConstantModule.get());
    std::string ir;
    llvm::raw_string_ostream stream(ir);
    ConstantModule->print(stream, nullptr);
    std::string err;
	auto copy = std::unique_ptr<llvm::Module>(llvm::CloneModule(ConstantModule.get()));
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

ClangNamespace* Analyzer::GetClangNamespace(ClangTU& tu, Location l, clang::DeclContext* con) {
    assert(con);
    if (ClangNamespaces.find(con) == ClangNamespaces.end())
        ClangNamespaces[con] = Wide::Memory::MakeUnique<ClangNamespace>(l, con, &tu, *this);
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
    auto types = GetFunctionParameters(skeleton->GetASTFunction(), skeleton->GetContext());
    if (WideFunctions.find(skeleton) == WideFunctions.end()
        || WideFunctions[skeleton].find(types) == WideFunctions[skeleton].end())
        WideFunctions[skeleton][types] = Wide::Memory::MakeUnique<Function>(*this, skeleton, types);
    return WideFunctions[skeleton][types].get();
}

Module* Analyzer::GetWideModule(const Parse::Module* p, Location higher, std::string name) {
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

ClangTemplateClass* Analyzer::GetClangTemplateClass(ClangTU& from, Location l, clang::ClassTemplateDecl* decl) {
    if (ClangTemplateClasses.find(decl) == ClangTemplateClasses.end())
        ClangTemplateClasses[decl] = Wide::Memory::MakeUnique<ClangTemplateClass>(decl, &from, l, *this);
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

UserDefinedType* Analyzer::GetUDT(const Parse::Type* t, Location context, std::string name) {
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
std::vector<Type*> Analyzer::GetFunctionParameters(const Parse::FunctionBase* func, Location context) {
    std::vector<Type*> out;
    if (HasImplicitThis(func, context)) {
        // If we're exported as an rvalue-qualified function, we need rvalue.
        if (auto astfun = dynamic_cast<const Parse::AttributeFunctionBase*>(func)) {
            for (auto&& attr : astfun->attributes) {
                if (auto name = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
                    if (auto string = boost::get<std::string>(&name->val.name)) {
                        if (*string == "export") {
                            auto expr = AnalyzeExpression(context, attr.initializer.get(), nullptr);
                            auto overset = dynamic_cast<OverloadSet*>(expr->GetType()->Decay());
                            if (!overset)
                                throw SpecificError<ExportNonOverloadSet>(*this, attr.initializer->location, "Attempted to export as a non-overload set.");
                            auto tuanddecl = overset->GetSingleFunction();
                            if (!tuanddecl.second) throw SpecificError<ExportNotSingleFunction>(*this, attr.initializer->location, "The overload set was not a single C++ function.");
                            auto tu = tuanddecl.first;
                            auto decl = tuanddecl.second;
                            if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                                if (!meth->isStatic()) {
                                    if (meth->getType()->getAs<clang::FunctionProtoType>()->getExtProtoInfo().RefQualifier == clang::RefQualifierKind::RQ_RValue)
                                        out.push_back(GetRvalueType(GetNonstaticContext(context)));
                                    else
                                        out.push_back(GetLvalueType(GetNonstaticContext(context)));
                                }
                            }
                        }
                    }
                }
            }
        }
        if (out.empty())
            out.push_back(GetLvalueType(GetNonstaticContext(context)));
    }
    for (auto&& arg : func->args) {
        auto ty_expr = arg.non_nullable_type.get();
        auto expr = AnalyzeExpression(context, ty_expr, nullptr);
        auto p_type = expr->GetType()->Decay();
        auto con_type = dynamic_cast<ConstructorType*>(p_type);
        if (!con_type)
            throw SpecificError<FunctionArgumentNotType>(*this, arg.non_nullable_type->location, "Function argument type was not a type.");
        if (arg.name == "this") {
            if (&arg == &func->args[0]) {
                if (!GetNonstaticContext(context))
                    throw SpecificError<ExplicitThisNoMember>(*this, arg.location, "Explicit this in a non-member function.");
                if (GetNonstaticContext(context) != con_type->GetConstructedType()->Decay())
                    throw SpecificError<ExplicitThisDoesntMatchMember>(*this, arg.location, "Explicit this's type did not match member type.");
            } else
                throw SpecificError<ExplicitThisNotFirstArgument>(*this, arg.location, "Explicit this was not the first argument.");
        }
        out.push_back(con_type->GetConstructedType());
    }
    return out;
}
bool Analyzer::HasImplicitThis(const Parse::FunctionBase* func, Location context) {
    // If we are a member without an explicit this, then we have an implicit this.
    if (!GetNonstaticContext(context))
        return false;
    if (func->args.size() > 0) {
        if (func->args[0].name == "this") {
            return false;
        }
    }
    return true;
}
Type* Semantic::GetNonstaticContext(Location context) {
    if (auto cpp = boost::get<Location::CppLocation>(&context.location))
        return cpp->types.empty() ? nullptr : cpp->types.back();
    auto wideloc = boost::get<Location::WideLocation>(context.location);
    if (wideloc.types.empty())
        return nullptr;
    auto ty = wideloc.types.back();
    if (auto lambda = boost::get<LambdaType*>(&ty))
        return *lambda;
    return boost::get<UserDefinedType*>(ty);
}
FunctionSkeleton* Analyzer::GetFunctionSkeleton(const Parse::FunctionBase* p, Location context) {
    auto location = context;
    if (auto astfun = dynamic_cast<const Parse::AttributeFunctionBase*>(p)) {
        for (auto&& attr : astfun->attributes) {
            if (auto name = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
                if (auto string = boost::get<std::string>(&name->val.name)) {
                    if (*string == "export") {
                        auto expr = AnalyzeExpression(context, attr.initializer.get(), nullptr);
                        auto overset = dynamic_cast<OverloadSet*>(expr->GetType()->Decay());
                        if (!overset)
                            throw SpecificError<ExportNonOverloadSet>(*this, attr.initializer->location, "Attempted to export as a non-overload set.");
                        auto tuanddecl = overset->GetSingleFunction();
                        if (!tuanddecl.second) throw SpecificError<ExportNotSingleFunction>(*this, attr.initializer->location, "The overload set was not a single C++ function.");
                        auto tu = tuanddecl.first;
                        auto decl = tuanddecl.second;
                        if (auto meth = llvm::dyn_cast<clang::CXXMethodDecl>(decl)) {
                            if (!meth->isStatic()) {
                                auto record = meth->getParent();
                                auto clangtype = (ClangType*)GetClangType(*tu, record->getASTContext().getRecordType(record));
                                location = clangtype->GetLocation();
                            }
                        }
                    }
                }
            }
        }
    }

    if (FunctionSkeletons.find(p) != FunctionSkeletons.end() && FunctionSkeletons[p].find(location) != FunctionSkeletons[p].end())
        return FunctionSkeletons[p][location].get();
    if (auto con_context = dynamic_cast<ConstructorContext*>(GetNonstaticContext(location))) {
        if (auto des = dynamic_cast<const Parse::Destructor*>(p)) {
            FunctionSkeletons[p][location] = std::make_unique<UserDefinedDestructor>(des, *this, location, con_context->GetConstructionMembers());
        }
        if (auto con = dynamic_cast<const Parse::Constructor*>(p)) {
            if (con->defaulted) {
                FunctionSkeletons[p][location] = std::make_unique<DefaultedConstructor>(con, *this, location, con_context->GetConstructionMembers());
            } else {
                FunctionSkeletons[p][location] = std::make_unique<UserDefinedConstructor>(con, *this, location, con_context->GetConstructionMembers());
            }
        }
        if (auto func = dynamic_cast<const Parse::Function*>(p)) {
            if (func->defaulted) {
                FunctionSkeletons[p][location] = std::make_unique<DefaultedAssignmentOperator>(func, *this, location, con_context->GetConstructionMembers());
            }
        }
    }

    if (FunctionSkeletons.find(p) == FunctionSkeletons.end() || FunctionSkeletons[p].find(location) == FunctionSkeletons[p].end())
        FunctionSkeletons[p][location] = std::make_unique<FunctionSkeleton>(p, *this, location);
    return FunctionSkeletons[p][location].get();
}

OverloadResolvable* Analyzer::GetCallableForFunction(const Parse::FunctionBase* func, Location context) {
    if (FunctionCallables.find(func) != FunctionCallables.end() && FunctionCallables[func].find(context) != FunctionCallables[func].end())
        return FunctionCallables.at(func).at(context).get();

    struct FunctionCallable : public OverloadResolvable {
        FunctionCallable(const Parse::FunctionBase* func, Location context)
            : func(func), context(context)
        {}
        
        const Parse::FunctionBase* func;
        Location context;

        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Location source) override final {
            // If we are a member and we have an explicit this then treat the first normally.
            // Else if we are a member, blindly accept whatever is given for argument 0 as long as it's the member type.
            // Else, treat the first argument normally.
            auto parameters = a.GetFunctionParameters(func, context);
            //if (dynamic_cast<const Parse::Lambda*>(skeleton->GetASTFunction()))
            //    if (dynamic_cast<LambdaType*>(types[0]->Decay()))
            //        if (types.size() == parameters.size() + 1)
            //            parameters.insert(parameters.begin(), types[0]);
            if (types.size() != parameters.size()) return Util::none;
            std::vector<Type*> result;
            for (unsigned i = 0; i < types.size(); ++i) {
                if (a.HasImplicitThis(func, context) && i == 0) {
                    // First, if no conversion is necessary.
                    if (Type::IsFirstASecond(types[i], parameters[i], source)) {
                        result.push_back(parameters[i]);
                        continue;
                    }
                    // If the parameter is-a nonstatic-context&&, then we're good. Let Function::AdjustArguments handle the adjustment, if necessary.
                    if (Type::IsFirstASecond(types[i], a.GetRvalueType(GetNonstaticContext(context)), source)) {
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

        Callable* GetCallableForResolution(std::vector<Type*> types, Location, Analyzer& a) override final {
            if (auto function = dynamic_cast<const Parse::Function*>(func))
                if (function->deleted)
                    return nullptr;
            if (auto con = dynamic_cast<const Parse::Constructor*>(func))
                if (con->deleted)
                    return nullptr;
            return a.GetWideFunction(a.GetFunctionSkeleton(func, context));
        }
    };
    FunctionCallables[func][context] = Wide::Memory::MakeUnique<FunctionCallable>(func, context);
    return FunctionCallables.at(func).at(context).get();
}

/*Parse::Access Semantic::GetAccessSpecifier(Type* from, Type* to) {
    auto source = from->Decay();
    auto target = to->Decay();
    if (source == target) return Parse::Access::Private; 
    if (source->IsDerivedFrom(target) == Type::InheritanceRelationship::UnambiguouslyDerived)
        return Parse::Access::Protected;
    if (auto context = source->GetContext())
        return GetAccessSpecifier(context, target);
    return Parse::Access::Public;
}*/
namespace {
    void ProcessFunction(const Parse::AttributeFunctionBase* f, Analyzer& a, Location l, std::string name, std::function<void(const Parse::AttributeFunctionBase*, std::string, Location)> callback) {
        bool exported = false;
        for (auto&& attr : f->attributes) {
            if (auto ident = dynamic_cast<const Parse::Identifier*>(attr.initialized.get()))
                if (auto str = boost::get<std::string>(&ident->val.name))
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
        callback(f, name, l);
    }
    template<typename T> void ProcessOverloadSet(std::unordered_set<std::shared_ptr<T>> set, Analyzer& a, Location m, std::string name, std::function<void(const Parse::AttributeFunctionBase*, std::string, Location)> callback) {
        for (auto&& func : set) {
            ProcessFunction(func.get(), a, m, name, callback);
        }
    }
}

void AnalyzeExportedFunctionsInModule(Analyzer& a, Location l, std::function<void(const Parse::AttributeFunctionBase*, std::string, Location)> callback) {
    auto mod = boost::get<Location::WideLocation>(l.location).modules.back()->GetASTModule();
    ProcessOverloadSet(mod->constructor_decls, a, l, "type", callback);
    ProcessOverloadSet(mod->destructor_decls, a, l, "~type", callback);
    for (auto name : mod->OperatorOverloads) {
        for (auto access : name.second) {
            ProcessOverloadSet(access.second, a, l, GetNameAsString(name.first), callback);
        }
    }
    for (auto&& decl : mod->named_decls) {
        if (auto overset = boost::get<std::unique_ptr<Parse::MultipleAccessContainer>>(&decl.second)) {
            if (auto funcs = dynamic_cast<Parse::ModuleOverloadSet<Parse::Function>*>(overset->get()))
                for (auto access : funcs->funcs)
                    ProcessOverloadSet(access.second, a, l, decl.first, callback);
        }
    }
}
void Semantic::AnalyzeExportedFunctions(Analyzer& a, std::function<void(const Parse::AttributeFunctionBase*, std::string, Location)> callback) {
    AnalyzeExportedFunctionsInModule(a, Location(a), callback);
}
void Semantic::AnalyzeExportedFunctions(Analyzer& a) {
    AnalyzeExportedFunctions(a, [&a](const Parse::AttributeFunctionBase* func, std::string name, Location l) {
        auto skeleton = a.GetFunctionSkeleton(func, l);
        auto function = a.GetWideFunction(skeleton);
        function->ComputeBody();
    });
}
Type* Analyzer::GetLiteralStringType() {
    if (!LiteralStringType)
        LiteralStringType = Wide::Memory::MakeUnique<StringType>(*this);
    return LiteralStringType.get();
}
LambdaType* Analyzer::GetLambdaType(const Parse::Lambda* lam, Location l, std::vector<std::pair<Parse::Name, Type*>> types) {
    if (LambdaTypes.find(lam) == LambdaTypes.end()
     || LambdaTypes[lam].find(types) == LambdaTypes[lam].end())
        LambdaTypes[lam][types] = Wide::Memory::MakeUnique<LambdaType>(lam, types, l, *this);
    return LambdaTypes[lam][types].get();
}
std::shared_ptr<Expression> Analyzer::AnalyzeExpression(Location lookup, const Parse::Expression* e, std::shared_ptr<Expression> _this) {
    static_assert(std::is_polymorphic<Parse::Expression>::value, "Expression must be polymorphic.");
    auto&& type_info = typeid(*e);
    if (ExpressionCache.find(e) == ExpressionCache.end()
        || ExpressionCache[e].find(lookup) == ExpressionCache[e].end())
        if (ExpressionHandlers.find(type_info) != ExpressionHandlers.end())
            ExpressionCache[e][lookup] = ExpressionHandlers[type_info](e, *this, lookup, _this);
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
    if (auto string = boost::get<std::string>(&name.name))
        return *string;
    return GetOperatorName(boost::get<Parse::OperatorName>(name.name));
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
std::function<void(CodegenContext&)> Semantic::ThrowObject(std::shared_ptr<Expression> expr, Context c) {
    // http://mentorembedded.github.io/cxx-abi/abi-eh.html
    // 2.4.2
    auto ty = expr->GetType()->Decay();
    auto RTTI = ty->GetRTTI();
    auto destructor_func = ty->GetDestructorFunctionForEH(c.from);
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
        auto exception = BuildChain(BuildChain(except_memory, Type::BuildInplaceConstruction(except_memory, { std::move(expr) }, c)), except_memory);
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
llvm::APInt Analyzer::EvaluateConstantIntegerExpression(std::shared_ptr<Expression> e) {
    assert(dynamic_cast<IntegralType*>(e->GetType(key)));
    assert(e->IsConstant(key));
    if (auto integer = dynamic_cast<Integer*>(e.get()))
        return integer->value;
    // BUG: Invoking MCJIT causes second MCJIT invocation to fail. This causes spurious test failures.
    auto evalfunc = llvm::Function::Create(llvm::FunctionType::get(e->GetType()->GetLLVMType(ConstantModule.get()), {}, false), llvm::GlobalValue::LinkageTypes::InternalLinkage, GetUniqueFunctionName(), ConstantModule.get());
    CodegenContext::EmitFunctionBody(evalfunc, [e](CodegenContext& con) {
        con->CreateRet(e->GetValue(con));
    });
    auto mod = Wide::Util::CloneModule(*ConstantModule);
    auto modptr = mod.get();
    llvm::EngineBuilder b(std::move(mod));
    b.setEngineKind(llvm::EngineKind::JIT);
    std::unique_ptr<llvm::ExecutionEngine> ee(b.create());
    ee->finalizeObject();
    auto result = ee->runFunction(modptr->getFunction(evalfunc->getName()), std::vector<llvm::GenericValue>());
    evalfunc->eraseFromParent();
    return result.IntVal;
}
std::shared_ptr<Statement> Semantic::AnalyzeStatement(Analyzer& analyzer, FunctionSkeleton* skel, const Parse::Statement* s, Location l, std::shared_ptr<Expression> _this) {
    if (analyzer.ExpressionHandlers.find(typeid(*s)) != analyzer.ExpressionHandlers.end())
        return analyzer.ExpressionHandlers[typeid(*s)](static_cast<const Parse::Expression*>(s), analyzer, l, _this);
    return analyzer.StatementHandlers[typeid(*s)](s, skel, analyzer, l, _this);
}

namespace {
    std::shared_ptr<Expression> LookupNameFromImport(Location context, Wide::Parse::Name name, Lexer::Range where, const Parse::Import* import) {
        auto&& a = context.GetAnalyzer();
        auto propagate = [=, &a]() -> std::shared_ptr<Expression> {
            if (import->previous) return LookupNameFromImport(context, name, where, import->previous.get());
            else return nullptr;
        };
        if (std::find(import->hidden.begin(), import->hidden.end(), name) != import->hidden.end())
            return propagate();
        if (import->names.size() != 0)
            if (std::find(import->names.begin(), import->names.end(), name) == import->names.end())
                return propagate();
        auto con = a.AnalyzeExpression(context, import->from.get(), nullptr);
        if (auto result = Type::AccessMember(con, name, { context, where })) {
            auto subresult = propagate();
            if (!subresult) return result;
            auto over1 = dynamic_cast<OverloadSet*>(result->GetType());
            auto over2 = dynamic_cast<OverloadSet*>(subresult->GetType());
            if (over1 && over2)
                return a.GetOverloadSet(over1, over2)->BuildValueConstruction({}, { context, where });
            throw SpecificError<ImportIdentifierLookupAmbiguous>(a, where, "Ambiguous lookup of name " + Semantic::GetNameAsString(name));
        }
        return propagate();
    }
    std::shared_ptr<Expression> LookupNameFromWide(Location::WideLocation l, Parse::Name name, Context c) {
        for (auto&& nonstatic : l.types) {
            if (auto udt = boost::get<UserDefinedType*>(&nonstatic)) {
                if (auto expr = (*udt)->AccessStaticMember(boost::get<std::string>(name.name), c))
                    return expr;
            }
        }
        for (auto&& module : l.modules) {
            auto instance = module->BuildValueConstruction({}, c);
            if (auto expr = Type::AccessMember(instance, name, c))
                return expr;
        }
        return nullptr;
    }
    std::shared_ptr<Expression> LookupNameFromCpp(Location::CppLocation l, Parse::Name name, Context c) {
        for (auto&& clangty : l.types) {
            if (auto expr = clangty->AccessStaticMember(boost::get<std::string>(name.name), c))
                return expr;
        }
        for (auto&& namespace_ : l.namespaces) {
            auto instance = namespace_->BuildValueConstruction({}, c);
            if (auto expr = Type::AccessMember(instance, name, c))
                return expr;
        }
        return nullptr;
    }
    std::shared_ptr<Expression> LookupNameFromContext(Location l, Parse::Name name, Context c) {
        if (auto wide = boost::get<Location::WideLocation>(&l.location))
            return LookupNameFromWide(*wide, name, c);
        return LookupNameFromCpp(boost::get<Location::CppLocation>(l.location), name, c);
    }
    std::shared_ptr<Expression> LookupNameFromThis(Location l, Parse::Name name, Lexer::Range where, std::shared_ptr<Expression> _this) {
        if (!_this)
            return nullptr;
        if (auto wide = boost::get<Location::WideLocation>(&l.location)) {
            if (auto lambda = boost::get<LambdaType*>(&wide->types.back())) {
                return (*lambda)->LookupCapture(_this, name);
            }
        }
        return Type::AccessMember(_this, name, { l, where });
    }
    std::shared_ptr<Expression> LookupNameFromLocalScope(Location l, Parse::Name name) {
        if (l.localscope) {
            return l.localscope->LookupLocal(boost::get<std::string>(name.name));
        }
        return nullptr;
    }
}
std::shared_ptr<Expression> Semantic::LookupName(Location l, Parse::Name name, Lexer::Range where, std::shared_ptr<Expression> _this, const Parse::Import* import) {
    // First, look up from local scope.
    if (auto expr = LookupNameFromLocalScope(l, name))
        return expr;
    
    // Then look up from "this".
    if (auto expr = LookupNameFromThis(l, name, where, _this))
        return expr;

    // Then look up from import and context.
    auto result = LookupNameFromContext(l, name, { l, where });
    auto result2 = import ? LookupNameFromImport(l, name, where, import) : nullptr;
    if (!result) return result2;
    if (!result2) return result;
    auto over1 = dynamic_cast<OverloadSet*>(result->GetType());
    auto over2 = dynamic_cast<OverloadSet*>(result2->GetType());
    if (over1 && over2)
        return l.GetAnalyzer().GetOverloadSet(over1, over2)->BuildValueConstruction({}, { l, where });
    throw SpecificError<IdentifierLookupAmbiguous>(l.GetAnalyzer(), where, "Ambiguous lookup of name " + Semantic::GetNameAsString(name));
}
std::unordered_set<Parse::Name> Semantic::GetLambdaCaptures(const Parse::Statement* s, Analyzer& a, std::unordered_set<Parse::Name>& local_names) {
    if (a.LambdaCaptureAnalyzers.find(typeid(*s)) == a.LambdaCaptureAnalyzers.end())
        return std::unordered_set<Parse::Name>();
    auto names = a.LambdaCaptureAnalyzers[typeid(*s)](s, a, local_names);
    auto copy = names;
    for (auto&& item : copy)
        if (local_names.find(item) != local_names.end())
            names.erase(item);
    return copy;
}