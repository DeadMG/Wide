#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/ClangType.h>
#include <Wide/Parser/AST.h>
#include <Wide/Semantic/Reference.h>
#include <sstream>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>
#include <clang/AST/Type.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ASTContext.h>
#include <clang/Sema/Sema.h>
#include <llvm/IR/DerivedTypes.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

namespace {
    bool HasAttribute(const Parse::DynamicFunction* func, std::string arg) {
        for (auto attr : func->attributes) {
            if (auto ident = dynamic_cast<const Parse::Identifier*>(attr.initialized)) {
                if (auto string = boost::get<std::string>(&ident->val)) {
                    if (*string == arg)
                        return true;
                }
            }
        }
        return false;
    }
    void AddAllBases(std::unordered_set<Type*>& all_bases, Type* root) {
        for (auto base : root->GetBases()) {
            all_bases.insert(base);
            AddAllBases(all_bases, base);
        }
    }
}
UserDefinedType::BaseData::BaseData(UserDefinedType* self) {
    for (auto expr : self->type->bases) {
        auto base = self->analyzer.AnalyzeExpression(self->context, expr);
        auto con = dynamic_cast<ConstructorType*>(base->GetType()->Decay());
        if (!con) throw NotAType(base->GetType(), expr->location);
        auto udt = con->GetConstructedType();
        if (udt == self) throw InvalidBase(con->GetConstructedType(), expr->location);
        if (udt->IsFinal()) throw InvalidBase(con->GetConstructedType(), expr->location);
        bases.push_back(udt);
        if (!PrimaryBase && !udt->GetVtableLayout().layout.empty())
            PrimaryBase = udt;
    }
}

UserDefinedType::VTableData::VTableData(UserDefinedType* self) {
    // If we have a primary base, our primary vtable starts with theirs.
    auto&& analyzer = self->analyzer;
    if (self->GetPrimaryBase()) funcs = self->GetPrimaryBase()->GetPrimaryVTable();
    is_abstract = false;
    std::unordered_map<const Parse::Function*, unsigned> primary_dynamic_functions;
    std::unordered_map<const Parse::Function*, Parse::Name> dynamic_functions;
    bool dynamic_destructor = self->type->destructor_decl ? self->type->destructor_decl->dynamic : false;
    std::unordered_set<Type*> all_bases;
    AddAllBases(all_bases, self);
    for (auto base : all_bases) {
        unsigned i = 0;
        for (auto func : base->GetPrimaryVTable().layout) {
            if (auto vfunc = boost::get<VTableLayout::VirtualFunction>(&func.func)) {
                if (vfunc->final) continue;
                if (self->type->functions.find(vfunc->name) == self->type->functions.end()) continue;
                std::unordered_set<const Parse::Function*> matches;
                auto set = self->type->functions.at(vfunc->name);
                for (auto access : set)
                    for (auto function : access.second)                        
                        if (FunctionType::CanThunkFromFirstToSecond(func.type, analyzer.GetWideFunction(function, self, GetNameAsString(vfunc->name))->GetSignature(), self))
                            matches.insert(function);
                if (matches.size() > 1) throw std::runtime_error("Too many valid matches for dynamic function override.");
                if (matches.empty()) {
                    if (vfunc->abstract)
                        is_abstract = true;
                    continue;
                }
                if (base == self->GetPrimaryBase())
                    primary_dynamic_functions[*matches.begin()] = i++;
                else
                    dynamic_functions[*matches.begin()] = vfunc->name;
            }
            if (auto specmem = boost::get<VTableLayout::SpecialMember>(&func.func)) {
                if (*specmem == VTableLayout::SpecialMember::Destructor)
                    dynamic_destructor = true;
            }
        }
    }
    // Add every function declared as dynamic or abstract.
    for (auto set : self->type->functions)
        for (auto access : set.second)
            for (auto func : access.second)
                if (func->dynamic || func->abstract)
                    dynamic_functions[func] = set.first;
    // If we don't have a primary base but we do have dynamic functions, first add offset and RTTI
    if (!self->GetPrimaryBase() && !dynamic_functions.empty()) {
        funcs.offset = 2;
        VTableLayout::VirtualFunctionEntry offset;
        VTableLayout::VirtualFunctionEntry rtti;
        offset.func = VTableLayout::SpecialMember::OffsetToTop;
        offset.type = nullptr;
        rtti.func = VTableLayout::SpecialMember::RTTIPointer;
        rtti.type = nullptr;
        funcs.layout.insert(funcs.layout.begin(), rtti);
        funcs.layout.insert(funcs.layout.begin(), offset);
    }

    // For every dynamic function that is not inherited from the primary base, add a slot.
    // Else, set the slot to the primary base's slot.
    for (auto func : dynamic_functions) {
        auto fty = analyzer.GetWideFunction(func.first, self, analyzer.GetFunctionParameters(func.first, self), GetNameAsString(func.second))->GetSignature();
        VTableLayout::VirtualFunction vfunc = {
            func.second,
            HasAttribute(func.first, "final"),
            func.first->abstract
        };
        if (primary_dynamic_functions.find(func.first) == primary_dynamic_functions.end()) {
            VTableIndices[func.first] = funcs.layout.size() - funcs.offset;
            funcs.layout.push_back({ vfunc, fty });
            continue;
        }
        VTableIndices[func.first] = primary_dynamic_functions[func.first] - funcs.offset;
        funcs.layout[primary_dynamic_functions[func.first]] = { vfunc, fty };
    }
    // If I have a dynamic destructor, that  isn't primary, then add it.
    if (dynamic_destructor && (!self->GetPrimaryBase() || !self->GetPrimaryBase()->HasVirtualDestructor())) {
        funcs.layout.push_back({ VTableLayout::SpecialMember::Destructor, nullptr });
        funcs.layout.push_back({ VTableLayout::SpecialMember::ItaniumABIDeletingDestructor, nullptr });
    }
}
UserDefinedType::MemberData::MemberData(UserDefinedType* self) {
    for (auto&& var : self->type->variables) {
        member_indices[var.name] = members.size();
        auto expr = self->analyzer.AnalyzeExpression(self->context, var.initializer);
        if (auto con = dynamic_cast<ConstructorType*>(expr->GetType()->Decay())) {
            members.push_back(con->GetConstructedType());
            NSDMIs.push_back(nullptr);
        } else {
            members.push_back(expr->GetType()->Decay());
            HasNSDMI = true;
            NSDMIs.push_back(var.initializer);
        }
        if (auto agg = dynamic_cast<AggregateType*>(members.back())) {
            if (agg == self || agg->HasMemberOfType(self))
                throw RecursiveMember(self, var.initializer->location);
        }
    }
}
UserDefinedType::MemberData::MemberData(MemberData&& other)
: members(std::move(other.members))
, NSDMIs(std::move(other.NSDMIs))
, HasNSDMI(other.HasNSDMI)
, member_indices(std::move(other.member_indices)) {}

UserDefinedType::MemberData& UserDefinedType::MemberData::operator=(MemberData&& other) {
    members = std::move(other.members);
    NSDMIs = std::move(other.NSDMIs);
    HasNSDMI = other.HasNSDMI;
    member_indices = std::move(other.member_indices);
    return *this;
}

UserDefinedType::UserDefinedType(const Parse::Type* t, Analyzer& a, Type* higher, std::string name)
: AggregateType(a)
, context(higher)
, type(t)
, source_name(name) 
{
}

std::vector<UserDefinedType::member> UserDefinedType::GetConstructionMembers() {
    std::vector<UserDefinedType::member> out;
    for (unsigned i = 0; i < type->bases.size(); ++i) {
        member m(type->bases[i]->location);
        m.t = GetBases()[i];
        m.num = { GetOffset(i) };
        out.push_back(std::move(m));
    }
    for (unsigned i = 0; i < type->variables.size(); ++i) {
        member m(type->variables[i].where);
        m.t = GetMembers()[i];
        m.name = type->variables[i].name;
        m.num = { GetOffset(i + type->bases.size()) };
        if (GetMemberData().NSDMIs[i])
            m.InClassInitializer = [this, i](std::shared_ptr<Expression>) { return analyzer.AnalyzeExpression(context, GetMemberData().NSDMIs[i]); };
        out.push_back(std::move(m));
    }
    return out;
}

std::shared_ptr<Expression> UserDefinedType::AccessMember(std::shared_ptr<Expression> self, std::string name, Context c) {
    auto spec = GetAccessSpecifier(c.from, this);
    if (GetMemberData().member_indices.find(name) != GetMemberData().member_indices.end()) {
        auto member = type->variables[GetMemberData().member_indices[name]];
        if (spec >= member.access)
            return PrimitiveAccessMember(std::move(self), GetMemberData().member_indices[name] + type->bases.size());
    }
    if (type->functions.find(name) != type->functions.end()) {
        std::unordered_set<OverloadResolvable*> resolvables;
        for (auto access : type->functions.at(name)) {
            if (spec >= access.first)
                for (auto func : access.second)
                    resolvables.insert(analyzer.GetCallableForFunction(func, this, GetNameAsString(name)));
        }
        auto selfty = self->GetType();
        if (!resolvables.empty())
            return analyzer.GetOverloadSet(resolvables, analyzer.GetRvalueType(selfty))->BuildValueConstruction({ std::move(self) }, c);
    }
    // Any of our bases have this member?
    Type* BaseType = nullptr;
    OverloadSet* BaseOverloadSet = nullptr;
    for (auto base : GetBases()) {
        auto baseobj = Type::AccessBase(self, base);
        if (auto member = base->AccessMember(std::move(baseobj), name, c)) {
            // If there's nothing there, we win.
            // If we're an OS and the existing is an OS, we win by unifying.
            // Else we lose.
            auto otheros = dynamic_cast<OverloadSet*>(member->GetType()->Decay());
            if (!BaseType) {
                if (otheros) {
                    if (BaseOverloadSet)
                        BaseOverloadSet = analyzer.GetOverloadSet(BaseOverloadSet, otheros, self->GetType());
                    else
                        BaseOverloadSet = otheros;
                    continue;
                }
                BaseType = base;
                continue;
            }
            throw AmbiguousLookup(GetNameAsString(name), base, BaseType, c.where);
        }
    }
    if (BaseOverloadSet)
        return BaseOverloadSet->BuildValueConstruction({ std::move(self) }, c);
    if (!BaseType)
        return nullptr;
    return BaseType->AccessMember(Type::AccessBase(std::move(self), BaseType), name, c);
}

Wide::Util::optional<clang::QualType> UserDefinedType::GetClangType(ClangTU& TU) {
    if (clangtypes.find(&TU) != clangtypes.end())
        return clangtypes[&TU];
    
    std::stringstream stream;
    // The name of the LLVM type is generated by AggregateType based on "this".
    // so downcast before using this to get the same name.
    // Should refactor someday
    stream << "__" << (AggregateType*)this;

    auto recdecl = clang::CXXRecordDecl::Create(TU.GetASTContext(), clang::TagDecl::TagKind::TTK_Struct, TU.GetDeclContext(), clang::SourceLocation(), clang::SourceLocation(), TU.GetIdentifierInfo(stream.str()));
    clangtypes[&TU] = TU.GetASTContext().getTypeDeclType(recdecl);
    recdecl->startDefinition();
    std::vector<clang::CXXBaseSpecifier*> basespecs;
    for (auto base : GetBases()) {
        if (!base->GetClangType(TU)) return Util::none;
        basespecs.push_back(new clang::CXXBaseSpecifier(
            clang::SourceRange(), 
            false, 
            false, 
            clang::AccessSpecifier::AS_public, 
            TU.GetASTContext().getTrivialTypeSourceInfo(*base->GetClangType(TU)),
            clang::SourceLocation()
        ));
    }
    recdecl->setBases(basespecs.data(), basespecs.size());
    auto Access = [](Parse::Access access) {
        switch (access) {
        case Parse::Access::Private:
            return clang::AccessSpecifier::AS_private;
        case Parse::Access::Public:
            return clang::AccessSpecifier::AS_public;
        case Parse::Access::Protected:
            return clang::AccessSpecifier::AS_protected;
        default:
            throw std::runtime_error("Wat- new access specifier?");
        }
    };
    for (std::size_t i = 0; i < type->variables.size(); ++i) {
        auto memberty = GetMembers()[i]->GetClangType(TU);
        if (!memberty) return Wide::Util::none;
        auto var = clang::FieldDecl::Create(
            TU.GetASTContext(),
            recdecl,
            clang::SourceLocation(),
            clang::SourceLocation(),
            TU.GetIdentifierInfo(type->variables[i].name),
            *memberty,
            nullptr,
            nullptr,
            false,
            clang::InClassInitStyle::ICIS_NoInit
        );
        var->setAccess(Access(type->variables[i].access));
        recdecl->addDecl(var);
    }
    auto GetArgsForFunc = [this](const Wide::Parse::FunctionBase* func) {
        return analyzer.GetFunctionParameters(func, this);
    };
    auto GetClangTypesForArgs = [this](std::vector<Type*> types, ClangTU& TU) -> Wide::Util::optional<std::vector<clang::QualType>> {
        std::vector<clang::QualType> args;
        for (auto&& ty : types)  {
            // Skip "this"
            if (&ty == &types.front()) continue;
            if (auto clangty = ty->GetClangType(TU))
                args.push_back(*clangty);
            else
                return Wide::Util::none;
        }
        return args;
    };
    auto GetParmVarDecls = [this](std::vector<clang::QualType> types, ClangTU& TU, clang::CXXRecordDecl* recdecl) {
        std::vector<clang::ParmVarDecl*> parms;
        for (auto qualty : types) {
            parms.push_back(clang::ParmVarDecl::Create(
                TU.GetASTContext(),
                recdecl,
                clang::SourceLocation(),
                clang::SourceLocation(),
                nullptr,
                qualty,
                TU.GetASTContext().getTrivialTypeSourceInfo(qualty),
                clang::VarDecl::StorageClass::SC_None,
                nullptr
            ));
        }
        return parms;
    };
    if (type->destructor_decl) {
        auto ty = clang::CanQualType::CreateUnsafe(TU.GetASTContext().getRecordType(recdecl));
        clang::FunctionProtoType::ExtProtoInfo ext_proto_info;
        std::vector<clang::QualType> args;
        auto fty = TU.GetASTContext().getFunctionType(*analyzer.GetVoidType()->GetClangType(TU), args, ext_proto_info);
        auto des = clang::CXXDestructorDecl::Create(
            TU.GetASTContext(),
            recdecl,
            clang::SourceLocation(),
            clang::DeclarationNameInfo(clang::DeclarationName(TU.GetASTContext().DeclarationNames.getCXXDestructorName(ty)), clang::SourceLocation()),
            fty,
            TU.GetASTContext().getTrivialTypeSourceInfo(fty),
            false,
            false
            );
        recdecl->addDecl(des);
        auto widedes = analyzer.GetWideFunction(type->destructor_decl, this, { analyzer.GetLvalueType(this) }, "~type");
        widedes->ComputeBody();
        widedes->AddExportName(TU.GetObject(des, clang::CXXDtorType::Dtor_Complete), widedes->GetSignature());
    }
    for (auto access : type->constructor_decls) {
        for (auto func : access.second) {
            auto types = GetArgsForFunc(func);
            auto ty = clang::CanQualType::CreateUnsafe(TU.GetASTContext().getRecordType(recdecl));
            clang::FunctionProtoType::ExtProtoInfo ext_proto_info;
            auto args = GetClangTypesForArgs(types, TU);
            if (!args) continue;
            auto fty = TU.GetASTContext().getFunctionType(*analyzer.GetVoidType()->GetClangType(TU), *args, ext_proto_info);
            auto con = clang::CXXConstructorDecl::Create(
                TU.GetASTContext(),
                recdecl,
                clang::SourceLocation(),
                clang::DeclarationNameInfo(clang::DeclarationName(TU.GetASTContext().DeclarationNames.getCXXConstructorName(ty)), clang::SourceLocation()),
                fty,
                TU.GetASTContext().getTrivialTypeSourceInfo(fty),
                true,
                false,
                false,
                false
                );
            con->setAccess(Access(access.first));
            con->setParams(GetParmVarDecls(*args, TU, recdecl));
            recdecl->addDecl(con);
            if (!func->deleted) {
                auto mfunc = analyzer.GetWideFunction(func, this, types, "type");
                mfunc->ComputeBody();
                mfunc->AddExportName(TU.GetObject(con, clang::CXXCtorType::Ctor_Complete), mfunc->GetSignature());
            } else
                con->setDeletedAsWritten(true);
        }
    }
    // TODO: Explicitly default all members we implicitly generated.
    for (auto overset : type->functions) {
        for (auto access : overset.second) {
            for (auto func : access.second) {
                if (IsMultiTyped(func)) continue;
                if (access.first == Parse::Access::Private) continue;
                auto types = GetArgsForFunc(func);
                std::vector<clang::QualType> args;
                for (auto&& ty : types)  {
                    // Skip "this"
                    if (&ty == &types.front()) continue;
                    if (auto clangty = ty->GetClangType(TU))
                        args.push_back(*clangty);
                }
                if (args.size() != types.size()) continue;
                auto get_return_type = [&] {
                    if (!func->deleted) {
                        auto mfunc = analyzer.GetWideFunction(func, this, types, GetNameAsString(overset.first));
                        return mfunc->GetSignature()->GetReturnType()->GetClangType(TU);
                    }
                    if (func->explicit_return) {
                        auto con = analyzer.AnalyzeExpression(this, func->explicit_return);
                        auto conty = dynamic_cast<ConstructorType*>(con->GetType()->Decay());
                        return conty->GetConstructedType()->GetClangType(TU);
                    }
                    return analyzer.GetVoidType()->GetClangType(TU);
                };
                auto ret = get_return_type();
                if (!ret) continue;
                clang::FunctionProtoType::ExtProtoInfo ext_proto_info;
                if (func->args.front().name == "this") {
                    if (types.front() == analyzer.GetLvalueType(this))
                        ext_proto_info.RefQualifier = clang::RefQualifierKind::RQ_LValue;
                    else
                        ext_proto_info.RefQualifier = clang::RefQualifierKind::RQ_RValue;
                }
                auto fty = TU.GetASTContext().getFunctionType(*ret, args, ext_proto_info);
                clang::DeclarationName name;
                if (auto string = boost::get<std::string>(&overset.first)) {
                    name = TU.GetIdentifierInfo(*string);
                } else {
                    auto opkind = GetTokenMappings().at(boost::get<Parse::OperatorName>(overset.first)).first;
                    name = TU.GetASTContext().DeclarationNames.getCXXOperatorName(opkind);
                }
                auto meth = clang::CXXMethodDecl::Create(
                    TU.GetASTContext(),
                    recdecl,
                    clang::SourceLocation(),
                    clang::DeclarationNameInfo(name, clang::SourceLocation()),
                    fty,
                    TU.GetASTContext().getTrivialTypeSourceInfo(fty),
                    clang::FunctionDecl::StorageClass::SC_None,
                    false,
                    false,
                    clang::SourceLocation()
                );
                meth->setAccess(Access(access.first));
                meth->setVirtualAsWritten(func->dynamic);
                meth->setParams(GetParmVarDecls(args, TU, recdecl));
                recdecl->addDecl(meth);
                if (func->deleted)
                    meth->setDeletedAsWritten(true);
                else {
                    auto mfunc = analyzer.GetWideFunction(func, this, types, GetNameAsString(overset.first));
                    mfunc->ComputeBody();
                    mfunc->AddExportName(TU.GetObject(meth), mfunc->GetSignature());
                }
            }
        }
    }
    recdecl->completeDefinition();
    TU.GetDeclContext()->addDecl(recdecl);
    analyzer.AddClangType(clangtypes[&TU], this);
    return clangtypes[&TU];
}

bool UserDefinedType::HasMember(Parse::Name name) {
    if (auto string = boost::get<std::string>(&name)) {
        return GetMemberData().member_indices.find(*string) != GetMemberData().member_indices.end();
    }
    return false;
}

std::vector<std::shared_ptr<Expression>> UserDefinedType::GetDefaultInitializerForMember(unsigned i) {
    if (GetMemberData().NSDMIs[i])
        return { analyzer.AnalyzeExpression(GetContext(), GetMemberData().NSDMIs[i]) };
    return {};
}

#pragma warning(disable : 4800)
UserDefinedType::DefaultData::DefaultData(UserDefinedType* self) {
    AggregateOps.copy_operator = true;
    AggregateOps.move_operator = true;
    AggregateCons.copy_constructor = true;
    AggregateCons.move_constructor = true;
    AggregateCons.default_constructor = true;
    IsComplex = false;

    if (self->type->destructor_decl && (!self->type->destructor_decl->defaulted || self->type->destructor_decl->dynamic))
        IsComplex = true;

    for (auto conset : self->type->constructor_decls) {
        for (auto con : conset.second) {
            if (IsMultiTyped(con)) continue;
            auto params = self->analyzer.GetFunctionParameters(con, self);
            if (params.size() > 2) continue; //???
            if (params.size() == 1) {
                AggregateCons.default_constructor = false;
                continue;
            }
            if (params[1] == self->analyzer.GetLvalueType(self)) {
                IsComplex = IsComplex || !con->defaulted;
                AggregateCons.copy_constructor = false;
            } else if (params[1] == self->analyzer.GetRvalueType(self)) {
                IsComplex = IsComplex || !con->defaulted;
                AggregateCons.move_constructor = false;
            }            
        }
    }

    Parse::Name name(Parse::OperatorName({ &Lexer::TokenTypes::Assignment }));
    if (self->type->functions.find(name) != self->type->functions.end())  {
        auto set = self->type->functions.at(name);
        for (auto op_access_pair : set) {
            auto ops = op_access_pair.second;
            for (auto op : ops) {
                if (IsMultiTyped(op)) continue;
                auto params = self->analyzer.GetFunctionParameters(op, self);
                if (params.size() > 2) continue; //???
                if (params[1] == self->analyzer.GetLvalueType(self)) {
                    IsComplex = IsComplex || (!op->defaulted || op->dynamic);
                    AggregateOps.copy_operator = false;
                } else if (params[1] == self->analyzer.GetRvalueType(self)) {
                    IsComplex = IsComplex || (!op->defaulted || op->dynamic);
                    AggregateOps.move_operator = false;
                }
            }
        }
    }
    
    if (IsComplex) {
        SimpleConstructors = self->analyzer.GetOverloadSet();
        SimpleAssOps = self->analyzer.GetOverloadSet();
        AggregateCons.copy_constructor = false;
        AggregateCons.default_constructor = false;
        AggregateCons.move_constructor = false;
        AggregateOps.copy_operator = false;
        AggregateOps.move_operator = false;
    } else {
        SimpleConstructors = self->analyzer.GetOverloadSet(self->AggregateType::CreateConstructorOverloadSet(AggregateCons), self->TupleInitializable::CreateConstructorOverloadSet(Parse::Access::Public));
        SimpleAssOps = self->CreateAssignmentOperatorOverloadSet(AggregateOps);
    }
}

OverloadSet* UserDefinedType::CreateConstructorOverloadSet(Parse::Access access) {
    auto user_defined = [&, this] {
        if (type->constructor_decls.empty())
            return analyzer.GetOverloadSet();
        std::unordered_set<OverloadResolvable*> resolvables;
        for (auto f : type->constructor_decls) {
            if (f.first <= access)
                for (auto func : f.second)
                    resolvables.insert(analyzer.GetCallableForFunction(func, this, "type"));
        }
        return analyzer.GetOverloadSet(resolvables, GetContext());
    };
    auto user_defined_constructors = user_defined();
    return analyzer.GetOverloadSet(user_defined_constructors, GetDefaultData().SimpleConstructors);
}

OverloadSet* UserDefinedType::CreateOperatorOverloadSet(Parse::OperatorName name, Parse::Access access) {
    auto user_defined = [&, this] {
        if (type->functions.find(name) != type->functions.end()) {
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto&& f : type->functions.at(name)) {
                if (f.first <= access)
                    for (auto func : f.second)
                        resolvable.insert(analyzer.GetCallableForFunction(func, this, GetNameAsString(name)));
            }
            return analyzer.GetOverloadSet(resolvable);
        }
        return analyzer.GetOverloadSet();
    };
    if (name.size() == 1 && name.front() == &Lexer::TokenTypes::Assignment)
        return analyzer.GetOverloadSet(user_defined(), GetDefaultData().SimpleAssOps);
    if (type->functions.find(name) != type->functions.end())
        return analyzer.GetOverloadSet(user_defined(), AggregateType::CreateOperatorOverloadSet(name, access));
    return AggregateType::CreateOperatorOverloadSet(name, access);
}

std::function<void(CodegenContext&)> UserDefinedType::BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize) {
    if (type->destructor_decl) {
        assert(self->GetType()->IsReference(this));
        std::unordered_set<OverloadResolvable*> resolvables;
        resolvables.insert(analyzer.GetCallableForFunction(type->destructor_decl, this, "~type"));
        auto desset = analyzer.GetOverloadSet(resolvables, self->GetType());
        auto callable = dynamic_cast<Wide::Semantic::Function*>(desset->Resolve({ self->GetType() }, c.from));
        auto call = callable->Call({ self }, { this, c.where });
        if (type->destructor_decl->dynamic && !devirtualize) 
            return [=](CodegenContext& c) {
                call->GetValue(c);
            };
        return [=](CodegenContext& c) {
            c->CreateCall(callable->EmitCode(c.module), self->GetValue(c));
        };
    }
    return AggregateType::BuildDestructorCall(self, c, true);
}

// Gotta override these to respect our user-defined functions
// else aggregatetype will just assume them.
// Could just return Type:: all the time but AggregateType will be faster.
bool UserDefinedType::IsCopyConstructible(Parse::Access access) {
    if (!type->constructor_decls.empty())
        return Type::IsCopyConstructible(access);
    return AggregateType::IsCopyConstructible(access);
}
bool UserDefinedType::IsMoveConstructible(Parse::Access access) {
    if (!type->constructor_decls.empty())
        return Type::IsMoveConstructible(access);
    return AggregateType::IsMoveConstructible(access);
}
bool UserDefinedType::IsCopyAssignable(Parse::Access access) {
    if (type->functions.find("operator=") != type->functions.end())
        return Type::IsCopyAssignable(access);
    return AggregateType::IsCopyAssignable(access);
}
bool UserDefinedType::IsMoveAssignable(Parse::Access access) {
    if (type->functions.find("operator=") != type->functions.end())
        return Type::IsMoveAssignable(access);
    return AggregateType::IsMoveAssignable(access);
}

Wide::Util::optional<std::vector<Type*>> UserDefinedType::GetTypesForTuple() {
    if (GetDefaultData().IsComplex)
        return Wide::Util::none;
    auto bases = GetBases();
    auto members = GetMembers();
    bases.insert(bases.end(), members.begin(), members.end());
    return bases;
}

// Implements TupleInitializable::PrimitiveAccessMember.
std::shared_ptr<Expression> UserDefinedType::PrimitiveAccessMember(std::shared_ptr<Expression> self, unsigned num) {
    return AggregateType::PrimitiveAccessMember(std::move(self), num);
}

std::string UserDefinedType::explain() {
    if (context == analyzer.GetGlobalModule())
        return source_name;
    return GetContext()->explain() + "." + source_name;
} 
Type::VTableLayout UserDefinedType::ComputePrimaryVTableLayout() {
    return GetVtableData().funcs;
}
llvm::Function* UserDefinedType::CreateDestructorFunction(llvm::Module* module) {
    if (!type->destructor_decl) return AggregateType::CreateDestructorFunction(module);
    auto desfunc = analyzer.GetWideFunction(type->destructor_decl, this, { analyzer.GetLvalueType(this) }, "~type");
    return desfunc->EmitCode(module);
}

std::pair<FunctionType*, std::function<llvm::Function*(llvm::Module*)>> UserDefinedType::VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry) {
    if (auto special = boost::get<VTableLayout::SpecialMember>(&entry.func)) {
        if (type->destructor_decl)
            analyzer.GetWideFunction(type->destructor_decl, this, "~type")->ComputeBody();
        return { analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this) }, false), [this](llvm::Module* mod) { return GetDestructorFunction(mod); } };
    }
    auto vfunc = boost::get<VTableLayout::VirtualFunction>(entry.func);
    auto name = vfunc.name;
    if (type->functions.find(name) == type->functions.end()) {
        // Could have been inherited from our primary base, if we have one.
        if (GetPrimaryBase()) {
            return GetPrimaryBase()->VirtualEntryFor(entry);
        }
        return {};
    }
    for (auto access : type->functions.at(name)) {
        for (auto func : access.second) {
            if (func->deleted) continue;
            if (IsMultiTyped(func)) continue;

            auto widefunc = analyzer.GetWideFunction(func, this, GetNameAsString(name));
            if (!FunctionType::CanThunkFromFirstToSecond(entry.type, widefunc->GetSignature(), this))
                continue;
            widefunc->ComputeBody();
            return { widefunc->GetSignature(), [widefunc](llvm::Module* module) { return widefunc->EmitCode(module); } };
        }
    }
    return {};
}

Type* UserDefinedType::GetVirtualPointerType() {
    if (GetBaseData().PrimaryBase)
        return GetBaseData().PrimaryBase->GetVirtualPointerType();
    return analyzer.GetFunctionType(analyzer.GetIntegralType(32, false), {}, false);
}

std::vector<std::pair<Type*, unsigned>> UserDefinedType::GetBasesAndOffsets() {
    std::vector<std::pair<Type*, unsigned>> out;
    for (unsigned i = 0; i < type->bases.size(); ++i) {
        out.push_back(std::make_pair(GetBaseData().bases[i], GetOffset(i)));
    }
    return out;
}
std::vector<Type*> UserDefinedType::GetBases() {
    return GetBaseData().bases;
}
Wide::Util::optional<unsigned> UserDefinedType::GetVirtualFunctionIndex(const Parse::DynamicFunction* func) {
    if (GetVtableData().VTableIndices.find(func) == GetVtableData().VTableIndices.end())
        return Wide::Util::none;
    return GetVtableData().VTableIndices.at(func);
}
bool UserDefinedType::IsSourceATarget(Type* source, Type* target, Type* context) {
    // We only have an interesting thing to say if target is a value.
    if (target == this) {
        if (!IsLvalueType(source)) {
            if (source->Decay()->IsDerivedFrom(this) == InheritanceRelationship::UnambiguouslyDerived && IsMoveConstructible(GetAccessSpecifier(context, this)))
                return true;
            return false;
        }
        if (source->Decay()->IsDerivedFrom(this) == InheritanceRelationship::UnambiguouslyDerived && IsCopyConstructible(GetAccessSpecifier(source, this)))
            return true;
        return false;
    }
    return false;
}
llvm::Constant* UserDefinedType::GetRTTI(llvm::Module* module) {
    // If we have a Clang type, then use it for compat.
    if (auto clangty = GetClangType(*analyzer.GetAggregateTU())) {
        return analyzer.GetAggregateTU()->GetItaniumRTTI(*clangty, module);
    }
    if (GetBases().size() == 0) {
        return AggregateType::GetRTTI(module);
    }
    std::stringstream stream;
    stream << "struct.__" << this;
    if (auto existing = module->getGlobalVariable(stream.str())) {
        return existing;
    }
    if (GetBases().size() == 1) {
        auto mangledname = GetGlobalString(stream.str(), module);
        auto vtable_name_of_rtti = "_ZTVN10__cxxabiv120__si_class_type_infoE";
        auto vtable = module->getOrInsertGlobal(vtable_name_of_rtti, llvm::Type::getInt8PtrTy(module->getContext()));
        vtable = llvm::ConstantExpr::getInBoundsGetElementPtr(vtable, llvm::ConstantInt::get(llvm::Type::getInt8Ty(module->getContext()), 2));
        vtable = llvm::ConstantExpr::getBitCast(vtable, llvm::Type::getInt8PtrTy(module->getContext()));
        std::vector<llvm::Constant*> inits = { vtable, mangledname, GetBases()[0]->GetRTTI(module) };
        auto ty = llvm::ConstantStruct::getTypeForElements(inits);
        auto rtti = new llvm::GlobalVariable(*module, ty, true, llvm::GlobalValue::LinkageTypes::LinkOnceODRLinkage, llvm::ConstantStruct::get(ty, inits), stream.str());
        return rtti;
    }
    // Multiple bases. Yay.
    auto mangledname = GetGlobalString(stream.str(), module);
    auto vtable_name_of_rtti = "_ZTVN10__cxxabiv121__vmi_class_type_infoE";
    auto vtable = module->getOrInsertGlobal(vtable_name_of_rtti, llvm::Type::getInt8PtrTy(module->getContext()));
    vtable = llvm::ConstantExpr::getInBoundsGetElementPtr(vtable, llvm::ConstantInt::get(llvm::Type::getInt8Ty(module->getContext()), 2));
    vtable = llvm::ConstantExpr::getBitCast(vtable, llvm::Type::getInt8PtrTy(module->getContext()));
    std::vector<llvm::Constant*> inits = { vtable, mangledname, GetBases()[0]->GetRTTI(module) };

    // Add flags.
    inits.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(module->getContext()), 0));

    // Add base count
    inits.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(module->getContext()), GetBases().size()));

    // Add one entry for every base.
    for (auto bases : GetBasesAndOffsets()) {
        inits.push_back(bases.first->GetRTTI(module));
        unsigned flags = 0x2 | (bases.second << 8);
        inits.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(module->getContext()), flags));
    }

    auto ty = llvm::ConstantStruct::getTypeForElements(inits);
    auto rtti = new llvm::GlobalVariable(*module, ty, true, llvm::GlobalValue::LinkageTypes::LinkOnceODRLinkage, llvm::ConstantStruct::get(ty, inits), stream.str());
    return rtti;
}
bool UserDefinedType::HasDeclaredDynamicFunctions() {
    for (auto overset : type->functions)
        for (auto access : overset.second)
            for (auto func : access.second)
                if (func->dynamic)
                    return true;
    if (type->destructor_decl)
        return type->destructor_decl->dynamic;
    return false;
}
UserDefinedType::BaseData::BaseData(BaseData&& other)
: bases(std::move(other.bases)), PrimaryBase(other.PrimaryBase) {}

UserDefinedType::BaseData& UserDefinedType::BaseData::operator=(BaseData&& other) {
    bases = std::move(other.bases);
    PrimaryBase = other.PrimaryBase;
    return *this;
}
UserDefinedType::VTableData::VTableData(VTableData&& other)
: funcs(std::move(other.funcs))
, VTableIndices(std::move(other.VTableIndices)) {}

UserDefinedType::VTableData& UserDefinedType::VTableData::operator=(VTableData&& other) {
    funcs = std::move(other.funcs);
    VTableIndices = std::move(other.VTableIndices);
    return *this;
}
Type* UserDefinedType::GetConstantContext() {
    if (type->destructor_decl)
        return nullptr;
    return AggregateType::GetConstantContext();
}
Wide::Util::optional<unsigned> UserDefinedType::SizeOverride() {
    for (auto attr : type->attributes) {
        if (auto ident = dynamic_cast<const Parse::Identifier*>(attr.initialized)) {
            if (auto string = boost::get<std::string>(&ident->val)) {
                if (*string == "size") {
                    auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer);
                    if (auto integer = dynamic_cast<Integer*>(expr->GetImplementation())) {
                        return integer->value.getLimitedValue();
                    }
                    throw std::runtime_error("Found size attribute but the initializing expression was not a constant integer.");
                }
            }
        }
    }
    return Util::none;
}
Wide::Util::optional<unsigned> UserDefinedType::AlignOverride() {
    for (auto attr : type->attributes) {
        if (auto ident = dynamic_cast<const Parse::Identifier*>(attr.initialized)) {
            if (auto string = boost::get<std::string>(&ident->val)) {
                if (*string == "alignment") {
                    auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer);
                    if (auto integer = dynamic_cast<Integer*>(expr->GetImplementation())) {
                        return integer->value.getLimitedValue();
                    }
                    throw std::runtime_error("Found size attribute but the initializing expression was not a constant integer.");
                }
            }
        }
    }
    return Util::none;
}
bool UserDefinedType::IsTriviallyCopyConstructible() {
    auto user_defined = [&, this] {
        if (type->constructor_decls.empty())
            return analyzer.GetOverloadSet();
        std::unordered_set<OverloadResolvable*> resolvables;
        for (auto f : type->constructor_decls) {
            for (auto func : f.second)
                resolvables.insert(analyzer.GetCallableForFunction(func, this, "type"));
        }
        return analyzer.GetOverloadSet(resolvables, analyzer.GetLvalueType(this));
    };
    auto user_defined_constructors = user_defined();
    if (user_defined_constructors->Resolve({ analyzer.GetLvalueType(this), analyzer.GetLvalueType(this) }, this))
        return false;
    return AggregateType::IsTriviallyCopyConstructible();
}
bool UserDefinedType::IsTriviallyDestructible() {
    return (!type->destructor_decl || (type->destructor_decl->defaulted && !type->destructor_decl->dynamic)) && AggregateType::IsTriviallyDestructible();
}
UserDefinedType::DefaultData::DefaultData(DefaultData&& other)
    : SimpleConstructors(other.SimpleConstructors)
    , SimpleAssOps(other.SimpleAssOps)
    , AggregateOps(other.AggregateOps)
    , AggregateCons(other.AggregateCons)
    , IsComplex(other.IsComplex) {}

UserDefinedType::DefaultData& UserDefinedType::DefaultData::operator=(DefaultData&& other) {
    SimpleConstructors = other.SimpleConstructors;
    SimpleAssOps = other.SimpleAssOps; 
    AggregateOps = other.AggregateOps;
    AggregateCons = other.AggregateCons;
    IsComplex = other.IsComplex;
    return *this;
}
std::shared_ptr<Expression> UserDefinedType::AccessStaticMember(std::string name, Context c) {
    auto spec = GetAccessSpecifier(c.from, this);
    if (GetMemberData().member_indices.find(name) != GetMemberData().member_indices.end()) {
        return nullptr;
    }
    if (type->functions.find(name) != type->functions.end()) {
        std::unordered_set<OverloadResolvable*> resolvables;
        for (auto access : type->functions.at(name)) {
            if (spec >= access.first)
                for (auto func : access.second)
                    resolvables.insert(analyzer.GetCallableForFunction(func, this, GetNameAsString(name)));
        }
        if (!resolvables.empty())
            return analyzer.GetOverloadSet(resolvables, nullptr)->BuildValueConstruction({}, c);
    }
    // Any of our bases have this member?
    Type* BaseType = nullptr;
    OverloadSet* BaseOverloadSet = nullptr;
    for (auto base : GetBaseData().bases) {
        if (auto member = base->AccessStaticMember(name, c)) {
            // If there's nothing there, we win.
            // If we're an OS and the existing is an OS, we win by unifying.
            // Else we lose.
            auto otheros = dynamic_cast<OverloadSet*>(member->GetType()->Decay());
            if (!BaseType) {
                if (otheros) {
                    if (BaseOverloadSet)
                        BaseOverloadSet = analyzer.GetOverloadSet(BaseOverloadSet, otheros);
                    else
                        BaseOverloadSet = otheros;
                    continue;
                }
                BaseType = base;
                continue;
            }
            throw AmbiguousLookup(GetNameAsString(name), base, BaseType, c.where);
        }
    }
    if (BaseOverloadSet)
        return BaseOverloadSet->BuildValueConstruction({}, c);
    if (!BaseType)
        return nullptr;
    return BaseType->AccessStaticMember(name, c);
}