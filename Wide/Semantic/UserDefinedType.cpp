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
#include <clang/Sema/Lookup.h>
#pragma warning(pop)

using namespace Wide;
using namespace Semantic;

namespace {
    bool HasAttribute(const Parse::DynamicFunction* func, std::string arg) {
        for (auto&& attr : func->attributes) {
            if (auto&& ident = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
                if (auto&& string = boost::get<std::string>(&ident->val)) {
                    if (*string == arg)
                        return true;
                }
            }
        }
        return false;
    }
    void AddAllBases(std::unordered_set<Type*>& all_bases, Type* root) {
        for (auto&&base : root->GetBases()) {
            all_bases.insert(base);
            AddAllBases(all_bases, base);
        }
    }
}
FunctionSkeleton* UserDefinedType::GetWideFunction(const Parse::FunctionBase* base, Parse::Name funcname) {
    return analyzer.GetWideFunction(base, this, GetNameAsString(funcname), [](Expression::InstanceKey key) -> Type* {
        if (!key) return nullptr;
        return Expression::GetArgumentType(key, 0);
    }, GetNonstaticLookup(base, funcname));
}
std::function<std::shared_ptr<Expression>(Wide::Parse::Name, Wide::Lexer::Range)> UserDefinedType::GetNonstaticLookup(Expression::InstanceKey key, std::shared_ptr<Expression> self) {
    return [=](Parse::Name name, Lexer::Range where) {
        return Type::AccessMember(key, self, name, { this, where });
    };
}
std::function<std::shared_ptr<Expression>(Wide::Parse::Name, Wide::Lexer::Range)> UserDefinedType::GetNonstaticLookup(const Parse::FunctionBase* base, Parse::Name funcname) {
    return [=](Parse::Name name, Lexer::Range where) -> std::shared_ptr<Expression> {
        auto skel = GetWideFunction(base, funcname);
        auto totally_not_this = CreatePrimGlobal(Range::Empty(), analyzer.GetLvalueType(this), [](CodegenContext& con) { return nullptr; });
        if (auto member = Type::AccessMember(Expression::NoInstance(), totally_not_this, name, { this, where }))
            return CreateResultExpression(Range::Empty(), [=](Expression::InstanceKey key) -> std::shared_ptr<Expression> {
                if (!key) return nullptr;
                auto func = analyzer.GetWideFunction(skel, *key);
                return Type::AccessMember(key, func->GetThis(), name, { this, where });
            });
        return nullptr;
    };
}
UserDefinedType::BaseData::BaseData(UserDefinedType* self) {
    for (auto&& expr : self->type->bases) {
        auto base = self->analyzer.AnalyzeExpression(self->context, expr.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
        auto con = dynamic_cast<ConstructorType*>(base->GetType(Expression::NoInstance())->Decay());
        if (!con) {
            base_errors.push_back(Wide::Memory::MakeUnique<SpecificError<BaseNotAType>>(self->analyzer, expr->location, "Base expression not a type."));
            continue;
        }
        auto udt = con->GetConstructedType();
        if (udt == self) {
            base_errors.push_back(Wide::Memory::MakeUnique<SpecificError<RecursiveBase>>(self->analyzer, expr->location, "Cannot inherit from self."));
            continue;
        }
        if (udt->IsFinal()) {
            base_errors.push_back(Wide::Memory::MakeUnique<SpecificError<BaseFinal>>(self->analyzer, expr->location, "Base is final."));
            continue;
        }
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
    dynamic_destructor = self->type->destructor_decl ? self->type->destructor_decl->dynamic : false;
    std::unordered_set<Type*> all_bases;
    AddAllBases(all_bases, self);
    for (auto&&base : all_bases) {
        unsigned i = 0;
        for (auto&&func : base->GetPrimaryVTable().layout) {
            if (auto&& vfunc = boost::get<VTableLayout::VirtualFunction>(&func.func)) {
                if (vfunc->final) continue;
                if (self->type->nonvariables.find(vfunc->name) == self->type->nonvariables.end()) continue;
                std::unordered_set<const Parse::Function*> matches;
                auto&& set_or_using = self->type->nonvariables.at(vfunc->name);
                if (!boost::get<Parse::OverloadSet<std::unique_ptr<Parse::Function>>>(&set_or_using)) continue;
                auto&& set = boost::get<Parse::OverloadSet<std::unique_ptr<Parse::Function>>>(set_or_using);
                for (auto&& access : set) {
                    for (auto&& function : access.second) {
                        auto skel = self->GetWideFunction(function.get(), GetNameAsString(vfunc->name));
                        if (FunctionType::CanThunkFromFirstToSecond(func.type, analyzer.GetWideFunction(skel)->GetSignature(), self, true))
                            matches.insert(function.get());
                    }
                }
                if (matches.size() > 1) throw SpecificError<VirtualOverrideAmbiguous>(analyzer, (*matches.begin())->where, "Virtual override is ambiguous.");
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
            if (auto&& specmem = boost::get<VTableLayout::SpecialMember>(&func.func)) {
                if (*specmem == VTableLayout::SpecialMember::Destructor)
                    dynamic_destructor = true;
            }
        }
    }
    // Add every function declared as dynamic or abstract.
    for (auto&& nonvar : self->type->nonvariables)
        if (auto&& set = boost::get<Parse::OverloadSet<std::unique_ptr<Parse::Function>>>(&nonvar.second))
            for (auto&& access : (*set))
                for (auto&& func : access.second)
                    if (func->dynamic || func->abstract)
                        dynamic_functions[func.get()] = nonvar.first;
    // If we don't have a primary base but we do have dynamic functions, first add offset and RTTI
    if (!self->GetPrimaryBase() && (!dynamic_functions.empty() || dynamic_destructor)) {
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
    for (auto&& func : dynamic_functions) {
        auto fty = analyzer.GetWideFunction(self->GetWideFunction(func.first, GetNameAsString(func.second)), analyzer.GetFunctionParameters(func.first, self))->GetSignature();
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
        VTableIndices[func.first] = primary_dynamic_functions[func.first];
        funcs.layout[primary_dynamic_functions[func.first]] = { vfunc, fty };
    }
    // If I have a dynamic destructor, that isn't primary, then add it.
    if (dynamic_destructor && (!self->GetPrimaryBase() || !self->GetPrimaryBase()->HasVirtualDestructor())) {
        auto functy = analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(self) }, false);
        funcs.layout.push_back({ VTableLayout::SpecialMember::Destructor, functy });
        funcs.layout.push_back({ VTableLayout::SpecialMember::ItaniumABIDeletingDestructor, functy });
        if (self->type->destructor_decl)
            VTableIndices[self->type->destructor_decl.get()] = (funcs.layout.size() - funcs.offset) - 2;
    }
}
bool UserDefinedType::HasVirtualDestructor(){
    return GetVtableData().dynamic_destructor;
}
UserDefinedType::MemberData::MemberData(UserDefinedType* self) {
    for (auto&& var : self->type->variables) {
        member_indices[var.name] = members.size();
        Type* explicit_type = nullptr;
        if (var.type) {
            auto expr = self->analyzer.AnalyzeExpression(self->context, var.type.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
            auto type = expr->GetType(Expression::NoInstance());
            if (!type)
                MemberErrors.push_back(Memory::MakeUnique<SpecificError<MemberNotAType>>(self->analyzer, var.type->location, "Member type not a type."));
            else 
                if (auto&& con = dynamic_cast<ConstructorType*>(type->Decay()))
                    members.push_back(explicit_type = con->GetConstructedType());
                else
                    MemberErrors.push_back(Memory::MakeUnique<SpecificError<MemberNotAType>>(self->analyzer, var.type->location, "Member type not a type."));
        }
        if (var.initializer) {
            auto expr = self->analyzer.AnalyzeExpression(self->context, var.initializer.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
            if (!explicit_type)
                members.push_back(expr->GetType(Expression::NoInstance())->Decay());
            HasNSDMI = true;
            NSDMIs.push_back(var.initializer.get());
        } else
            NSDMIs.push_back(nullptr);
        if (auto&& agg = dynamic_cast<AggregateType*>(members.back())) {
            if (agg == self || agg->HasMemberOfType(self)) {
                MemberErrors.push_back(Memory::MakeUnique<SpecificError<RecursiveMember>>(self->analyzer, var.type ? var.type->location : var.initializer->location, "Member is recursive."));
                members.pop_back();
            }
        }
    }
    for (auto&& tuple : self->type->imports) {
        auto expr = self->analyzer.AnalyzeExpression(self->context, std::get<0>(tuple).get(), [](Parse::Name, Lexer::Range) { return nullptr; });
        auto conty = dynamic_cast<ConstructorType*>(expr->GetType(Expression::NoInstance())->Decay());
        if (!conty) {
            ImportErrors.push_back(Wide::Memory::MakeUnique<SpecificError<ImportNotAType>>(self->analyzer, std::get<0>(tuple)->location, "Import expression was not a type."));
        }
        auto basety = conty->GetConstructedType();
        for (auto&& name : std::get<1>(tuple))
            BaseImports[name].insert(std::make_pair(basety, std::get<0>(tuple)->location));
        if (std::get<2>(tuple))
            imported_constructors[basety] = nullptr;
    }
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
        m.num = [this, i] { return GetOffset(i); };
        out.push_back(std::move(m));
    }
    for (unsigned i = 0; i < type->variables.size(); ++i) {
        member m(type->variables[i].where);
        m.t = GetMembers()[i];
        m.name = type->variables[i].name;
        m.num = [this, i] { return GetOffset(i + type->bases.size()); };
        if (GetMemberData().NSDMIs[i])
            m.InClassInitializer = [this, i](std::shared_ptr<Expression>) { return analyzer.AnalyzeExpression(context, GetMemberData().NSDMIs[i], [](Parse::Name, Lexer::Range) { return nullptr; }); };
        out.push_back(std::move(m));
    }
    return out;
}

std::shared_ptr<Expression> UserDefinedType::AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> self, std::string name, Context c) {
    auto spec = GetAccessSpecifier(c.from, this);
    if (GetMemberData().member_indices.find(name) != GetMemberData().member_indices.end()) {
        auto&& member = type->variables[GetMemberData().member_indices[name]];
        if (spec >= member.access)
            return PrimitiveAccessMember(std::move(self), GetMemberData().member_indices[name] + type->bases.size());
    }
    if (type->nonvariables.find(name) != type->nonvariables.end()) {
        if (auto&& set = boost::get<Parse::OverloadSet<std::unique_ptr<Parse::Function>>>(&type->nonvariables.at(name))) {
            std::unordered_set<OverloadResolvable*> resolvables;
            for (auto&& access : *set) {
                if (spec >= access.first)
                    for (auto&& func : access.second)
                        resolvables.insert(analyzer.GetCallableForFunction(analyzer.GetWideFunction(func.get(), this, GetNameAsString(name), this, GetNonstaticLookup(func.get(), name))));
            }
            // Check for imports.
            OverloadSet* imports = analyzer.GetOverloadSet();
            if (GetMemberData().BaseImports.find(name) != GetMemberData().BaseImports.end()) {
                for (auto&&base : GetMemberData().BaseImports.at(name)) {
                    if (IsDerivedFrom(base.first) != InheritanceRelationship::UnambiguouslyDerived) {
                        if (FunctionImportErrors.find(name) == FunctionImportErrors.end()
                         || FunctionImportErrors[name].find(base.first) == FunctionImportErrors[name].end())
                            FunctionImportErrors[name][base.first] = Wide::Memory::MakeUnique<SpecificError<ImportNotUnambiguousBase>>(analyzer, base.second, "Import type was not an unambiguous base.");
                        continue;
                    }
                    auto baseobj = Type::AccessBase(key, self, base.first);
                    auto member = Type::AccessMember(key, baseobj, name, c);
                    if (!member) continue;
                    auto os = dynamic_cast<OverloadSet*>(member->GetType(Expression::NoInstance())->Decay());
                    if (!os){
                        if (FunctionImportErrors.find(name) == FunctionImportErrors.end()
                         || FunctionImportErrors[name].find(base.first) == FunctionImportErrors[name].end())
                         FunctionImportErrors[name][base.first] = Wide::Memory::MakeUnique<SpecificError<ImportNotOverloadSet>>(analyzer, base.second, "Import did not name an overload set.");
                        continue;
                    }
                    imports = analyzer.GetOverloadSet(imports, os);
                }
            }
            if (!resolvables.empty() || imports != analyzer.GetOverloadSet())
                return CreateResultExpression(Range::Elements(self), [=](Expression::InstanceKey key) {
                    return analyzer.GetOverloadSet(imports, analyzer.GetOverloadSet(resolvables), analyzer.GetRvalueType(self->GetType(key)))->BuildValueConstruction(key, { self }, c);
                });
        } else {
            auto&& use = boost::get<std::pair<Parse::Access, std::unique_ptr<Parse::Using>>>(type->nonvariables.at(name));
            if (spec >= use.first) {
                return BuildChain(self, analyzer.AnalyzeExpression(context, use.second->expr.get(), GetNonstaticLookup(key, self)));
            }
        }
    }
    // Any of our bases have this member?
    std::vector<std::shared_ptr<Expression>> basemembers;
    for (auto&&base : GetBases()) {
        auto baseobj = Type::AccessBase(key, self, base);
        if (auto&& member = Type::AccessMember(key, std::move(baseobj), name, c))
            basemembers.push_back(member);
    }
    if (basemembers.empty()) return nullptr;
    if (basemembers.size() == 1) return basemembers[0];
    OverloadSet* BaseOverloadSet = analyzer.GetOverloadSet();
    for (auto&& member : basemembers) {
        auto otheros = dynamic_cast<OverloadSet*>(member->GetType(key)->Decay());
        if (otheros) {
            if (BaseOverloadSet)
                BaseOverloadSet = analyzer.GetOverloadSet(BaseOverloadSet, otheros, self->GetType(key));
            else
                BaseOverloadSet = otheros;
            continue;
        }
        throw SpecificError<AmbiguousMemberLookup>(analyzer, c.where, "Member lookup was ambiguous in base classes.");
    }
    if (BaseOverloadSet)
        return BaseOverloadSet->BuildValueConstruction(key, { std::move(self) }, c);
    return nullptr;
}

Wide::Util::optional<clang::QualType> UserDefinedType::GetClangType(ClangTU& TU) {
    //if (SizeOverride()) return Wide::Util::none;
    //if (AlignOverride()) return Wide::Util::none;
    if (clangtypes.find(&TU) != clangtypes.end())
        return TU.GetASTContext().getRecordType(clangtypes[&TU]);
    
    auto recdecl = clang::CXXRecordDecl::Create(TU.GetASTContext(), clang::TagDecl::TagKind::TTK_Struct, TU.GetDeclContext(), clang::SourceLocation(), clang::SourceLocation(), TU.GetIdentifierInfo(GetLLVMTypeName()));
    clangtypes[&TU] = recdecl;
    recdecl->setHasExternalLexicalStorage(true);
    ClangTypeInfo info;
    info.ty = this;
    info.Layout = [this, &TU, recdecl](
        uint64_t& size,
        uint64_t& alignment,
        llvm::DenseMap<const clang::FieldDecl*, uint64_t>& fields,
        llvm::DenseMap<const clang::CXXRecordDecl*, clang::CharUnits>& bases,
        llvm::DenseMap<const clang::CXXRecordDecl*, clang::CharUnits>&
    ) {
        size = this->size() * 8;
        alignment = this->alignment() * 8;
        
        for (unsigned i = 0; i < GetBases().size(); ++i) {
            auto recdecl = (*GetBases()[i]->GetClangType(TU))->getAsCXXRecordDecl();
            bases[recdecl] = clang::CharUnits::fromQuantity(GetOffset(i));
        }
        for (auto&&mem : GetMemberData().member_indices) {
            auto offset = GetOffset(mem.second + type->bases.size());
            clang::LookupResult lr(
                TU.GetSema(),
                clang::DeclarationNameInfo(clang::DeclarationName(TU.GetIdentifierInfo(mem.first)), clang::SourceLocation()),
                clang::Sema::LookupNameKind::LookupOrdinaryName);
            lr.suppressDiagnostics();
            auto result = TU.GetSema().LookupQualifiedName(lr, recdecl);
            assert(result);
            assert(lr.isSingleResult());
            fields[lr.getAsSingle<clang::FieldDecl>()] = offset * 8;
        }
    };
    info.Complete = [this, recdecl, &TU] {
        auto Access = [](Parse::Access access) -> clang::AccessSpecifier {
            switch (access) {
            case Parse::Access::Private:
                return clang::AccessSpecifier::AS_private;
            case Parse::Access::Public:
                return clang::AccessSpecifier::AS_public;
            case Parse::Access::Protected:
                return clang::AccessSpecifier::AS_protected;
            }
        };
        
        auto GetArgsForFunc = [this](const Wide::Parse::FunctionBase* func) {
            return analyzer.GetFunctionParameters(func, this);
        };

        auto GetClangTypesForArgs = [this](std::vector<Type*> types, ClangTU& TU) -> Wide::Util::optional<std::vector<clang::QualType>> {
            std::vector<clang::QualType> args;
            for (auto&& ty : types)  {
                // Skip "this"
                if (&ty == &types.front()) continue;
                if (auto&& clangty = ty->GetClangType(TU))
                    args.push_back(*clangty);
                else
                    return Wide::Util::none;
            }
            return args;
        };

        auto GetParmVarDecls = [this](std::vector<clang::QualType> types, ClangTU& TU, clang::CXXMethodDecl* methdecl) {
            std::vector<clang::ParmVarDecl*> parms;
            for (auto&&qualty : types) {
                parms.push_back(clang::ParmVarDecl::Create(
                    TU.GetASTContext(),
                    methdecl,
                    clang::SourceLocation(),
                    clang::SourceLocation(),
                    nullptr,
                    qualty,
                    TU.GetASTContext().getTrivialTypeSourceInfo(qualty),
                    clang::StorageClass::SC_None,
                    nullptr
                ));
            }
            return parms;
        };

        std::vector<clang::CXXBaseSpecifier*> basespecs;
        for (auto&&base : GetBases()) {
            if (!base->GetClangType(TU)) return;
            basespecs.push_back(new clang::CXXBaseSpecifier(
                clang::SourceRange(),
                false,
                false,
                clang::AccessSpecifier::AS_public,
                TU.GetASTContext().getTrivialTypeSourceInfo(*base->GetClangType(TU)),
                clang::SourceLocation()
            ));
        }

        std::vector<clang::FieldDecl*> fields;
        for (std::size_t i = 0; i < type->variables.size(); ++i) {
            auto memberty = GetMembers()[i]->GetClangType(TU);
            if (!memberty) return;
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
            fields.push_back(var);
        }

        clang::CXXDestructorDecl* des = nullptr;
        if (type->destructor_decl) {
            auto ty = clang::CanQualType::CreateUnsafe(TU.GetASTContext().getRecordType(recdecl));
            clang::FunctionProtoType::ExtProtoInfo ext_proto_info;
            std::vector<clang::QualType> args;
            auto fty = TU.GetASTContext().getFunctionType(*analyzer.GetVoidType()->GetClangType(TU), args, ext_proto_info);
            des = clang::CXXDestructorDecl::Create(
                TU.GetASTContext(),
                recdecl,
                clang::SourceLocation(),
                clang::DeclarationNameInfo(clang::DeclarationName(TU.GetASTContext().DeclarationNames.getCXXDestructorName(ty)), clang::SourceLocation()),
                fty,
                TU.GetASTContext().getTrivialTypeSourceInfo(fty),
                false,
                false
                );
            if (HasVirtualDestructor())
                des->setVirtualAsWritten(true);
            auto widedes = analyzer.GetWideFunction(GetWideFunction(type->destructor_decl.get(), "~type"), { analyzer.GetLvalueType(this) });
            widedes->ComputeBody();
            widedes->AddExportName(GetFunctionType(des, TU, analyzer)->CreateThunk(TU.GetObject(analyzer, des, clang::CodeGen::StructorType::Complete), widedes->GetStaticSelf(), des, this));
        }

        std::vector<clang::CXXConstructorDecl*> cons;
        for (auto&& access : type->constructor_decls) {
            for (auto&& func : access.second) {
                auto types = GetArgsForFunc(func.get());
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
                con->setParams(GetParmVarDecls(*args, TU, con));
                cons.push_back(con);
                if (!func->deleted) {
                    auto mfunc = analyzer.GetWideFunction(GetWideFunction(func.get(), "type"), types);
                    mfunc->ComputeBody();
                    mfunc->AddExportName(GetFunctionType(con, TU, analyzer)->CreateThunk(TU.GetObject(analyzer, con, clang::CodeGen::StructorType::Complete), mfunc->GetStaticSelf(), con, this));
                } else
                    con->setDeletedAsWritten(true);
            }
        }

        std::vector<clang::CXXMethodDecl*> methods;
        // TODO: Explicitly default all members we implicitly generated.
        for (auto&& nonvar : type->nonvariables) {
            auto maybe_overset = boost::get<Parse::OverloadSet<std::unique_ptr<Parse::Function>>>(&nonvar.second);
            if (!maybe_overset) continue;
            for (auto&& access : *maybe_overset) {
                for (auto&& func : access.second) {
                    if (access.first == Parse::Access::Private) continue;
                    auto types = GetArgsForFunc(func.get());
                    std::vector<clang::QualType> args;
                    for (auto&& ty : types)  {
                        // Skip "this"
                        if (&ty == &types.front()) continue;
                        if (auto&& clangty = ty->GetClangType(TU))
                            args.push_back(*clangty);
                    }
                    auto get_return_type = [&] {
                        if (!func->deleted) {
                            auto mfunc = analyzer.GetWideFunction(GetWideFunction(func.get(), GetNameAsString(nonvar.first)), types);
                            return mfunc->GetSignature()->GetReturnType()->GetClangType(TU);
                        }
                        if (func->explicit_return) {
                            auto con = analyzer.AnalyzeExpression(this, func->explicit_return.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
                            auto conty = dynamic_cast<ConstructorType*>(con->GetType(Expression::NoInstance())->Decay());
                            return conty->GetConstructedType()->GetClangType(TU);
                        }
                        return analyzer.GetVoidType()->GetClangType(TU);
                    };
                    auto ret = get_return_type();
                    if (!ret) continue;
                    clang::FunctionProtoType::ExtProtoInfo ext_proto_info;
                    if (!func->args.empty()) {
                        if (func->args.front().name == "this") {
                            if (types.front() == analyzer.GetLvalueType(this))
                                ext_proto_info.RefQualifier = clang::RefQualifierKind::RQ_LValue;
                            else
                                ext_proto_info.RefQualifier = clang::RefQualifierKind::RQ_RValue;
                        }
                    }
                    auto fty = TU.GetASTContext().getFunctionType(*ret, args, ext_proto_info);
                    clang::DeclarationName name;
                    if (auto&& string = boost::get<std::string>(&nonvar.first)) {
                        name = TU.GetIdentifierInfo(*string);
                    } else {
                        auto op = boost::get<Parse::OperatorName>(nonvar.first);
                        if (op == Parse::OperatorName{ &Lexer::TokenTypes::QuestionMark }) {
                            name = TU.GetASTContext().DeclarationNames.getCXXConversionFunctionName(TU.GetASTContext().BoolTy);
                        } else {
                            auto opkind = GetTokenMappings().at(boost::get<Parse::OperatorName>(nonvar.first)).first;
                            name = TU.GetASTContext().DeclarationNames.getCXXOperatorName(opkind);
                        }
                    }
                    auto meth = clang::CXXMethodDecl::Create(
                        TU.GetASTContext(),
                        recdecl,
                        clang::SourceLocation(),
                        clang::DeclarationNameInfo(name, clang::SourceLocation()),
                        fty,
                        TU.GetASTContext().getTrivialTypeSourceInfo(fty),
                        clang::StorageClass::SC_None,
                        false,
                        false,
                        clang::SourceLocation()
                        );
                    meth->setAccess(Access(access.first));
                    meth->setVirtualAsWritten(func->dynamic);
                    meth->setParams(GetParmVarDecls(args, TU, meth));
                    // TODO: Set explicit for bool conversion operators.
                    methods.push_back(meth);
                    if (func->deleted)
                        meth->setDeletedAsWritten(true);
                    else {
                        auto mfunc = analyzer.GetWideFunction(GetWideFunction(func.get(), GetNameAsString(nonvar.first)), types);
                        mfunc->ComputeBody();
                        mfunc->AddExportName(GetFunctionType(meth, TU, analyzer)->CreateThunk(TU.GetObject(analyzer, meth), mfunc->GetStaticSelf(), meth, this));
                    }
                }
            }
        }
        recdecl->startDefinition();
        recdecl->setBases(basespecs.data(), basespecs.size());
        if (des)
            recdecl->addDecl(des);
        for (auto&&field : fields)
            recdecl->addDecl(field);
        for (auto&&con : cons)
            recdecl->addDecl(con);
        for (auto&&meth : methods)
            recdecl->addDecl(meth);
        recdecl->completeDefinition();
        recdecl->getDefinition()->setHasExternalLexicalStorage(true);
    };
    analyzer.AddClangType(clangtypes[&TU], info);
    TU.GetDeclContext()->addDecl(recdecl);
    return TU.GetASTContext().getRecordType(clangtypes[&TU]);
}

bool UserDefinedType::HasMember(Parse::Name name) {
    if (auto&& string = boost::get<std::string>(&name)) {
        return GetMemberData().member_indices.find(*string) != GetMemberData().member_indices.end();
    }
    return false;
}

std::vector<std::shared_ptr<Expression>> UserDefinedType::GetDefaultInitializerForMember(unsigned i) {
    if (GetMemberData().NSDMIs[i])
        return { analyzer.AnalyzeExpression(GetContext(), GetMemberData().NSDMIs[i], [](Parse::Name, Lexer::Range) { return nullptr; }) };
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

    for (auto&& conset : self->type->constructor_decls) {
        for (auto&& con : conset.second) {
            auto params = self->analyzer.GetFunctionParameters(con.get(), self);
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
    if (self->type->nonvariables.find(name) != self->type->nonvariables.end())  {
        auto&& nonvar = self->type->nonvariables.at(name);
        if (auto&& set = boost::get<Parse::OverloadSet<std::unique_ptr<Parse::Function>>>(&nonvar)) {
            for (auto&& op_access_pair : *set) {
                auto&& ops = op_access_pair.second;
                for (auto&& op : ops) {
                    auto params = self->analyzer.GetFunctionParameters(op.get(), self);
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

struct Wide::Semantic::UserDefinedType::ImportConstructorCallable : Callable {
    // Don't need to do that much ABI handling here fortunately.
    ImportConstructorCallable(Callable* target, UserDefinedType* self, Type* base, std::vector<Type*> args)
        : base_callable(target), self(self), target_base(base), arguments(args), thunk_function(nullptr) {}
    std::vector<std::shared_ptr<Expression>> AdjustArguments(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
        auto adjusted = base_callable->AdjustArguments(key, args, c);
        adjusted[0] = Wide::Semantic::AdjustArgumentsForTypes(key, { args[0] }, { self->analyzer.GetLvalueType(self) }, c)[0];
        return adjusted;
    }
    std::shared_ptr<Expression> CallFunction(Expression::InstanceKey key, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
        CreateInitializers(c);
        return Type::BuildCall(Expression::NoInstance(), CreatePrimGlobal(Range::Empty(), GetFunctionType(), [this](CodegenContext& con) {
            EmitCode(con);
            return thunk_function;
        }), args, c);
    }
    void CreateInitializers(Context c) {
        if (initializers) return;       
        auto Argument = [=](unsigned num) {
            return CreatePrimGlobal(Range::Empty(), arguments[num], [=](CodegenContext& con) {
                return std::next(thunk_function->arg_begin(), num);
            });
        };
        auto MemberConstructionAccess = [=](Expression::InstanceKey key, Type* member, Lexer::Range where, std::shared_ptr<Expression> Construction, std::shared_ptr<Expression> memexpr) {
            std::function<void(CodegenContext&)> destructor;
            if (!member->IsTriviallyDestructible())
                destructor = member->BuildDestructorCall(key, memexpr, { member, where }, true);
            return CreatePrimGlobal(Range::Elements(Construction), Construction->GetType(key), [=](CodegenContext& con) {
                auto val = Construction->GetValue(con);
                if (destructor)
                    con.AddExceptionOnlyDestructor(destructor);
                return val;
            });
        };
        auto GetMemberFromThis = [](std::shared_ptr<Expression> self, std::function<unsigned()> offset, Type* member) {
            return CreatePrimUnOp(self, member, [offset, member](llvm::Value* val, CodegenContext& con) {
                auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
                self = con->CreateConstGEP1_32(self, offset());
                return con->CreatePointerCast(self, member->GetLLVMType(con));
            });
        };
        auto this_ = Argument(0);
        std::vector<std::shared_ptr<Expression>> inits;
        unsigned i = 0;
        for (auto&&base : self->GetBases()) {
            if (base == target_base) {
                std::vector<std::shared_ptr<Expression>> args;
                args.push_back(Type::AccessBase(Expression::NoInstance(), this_, base));
                for (auto&&i = 1; i < arguments.size(); ++i)
                    args.push_back(Argument(i));
                inits.push_back(base_callable->CallFunction(arguments, args, c));
                continue;
            }
            auto init = Type::BuildInplaceConstruction(Expression::NoInstance(), Type::AccessBase(Expression::NoInstance(), this_, base), {}, c);
            inits.push_back(MemberConstructionAccess(arguments, base, c.where, init, Type::AccessBase(Expression::NoInstance(), this_, base)));
        }
        for (auto&&member : self->GetMembers()) {
            auto lhs = GetMemberFromThis(this_, [this, i] { return self->GetOffset(i); }, member->analyzer.GetLvalueType(member));
            auto init = member->BuildInplaceConstruction(Expression::NoInstance(), lhs, self->GetDefaultInitializerForMember(i++), c);
            inits.push_back(MemberConstructionAccess(arguments, member, c.where, init, lhs));
        }
        inits.push_back(Type::SetVirtualPointers(Expression::NoInstance(), this_));
        initializers = inits;
    }
    void EmitCode(llvm::Module* module) {
        if (thunk_function) return;
        thunk_function = llvm::Function::Create(llvm::cast<llvm::FunctionType>(GetFunctionType()->GetLLVMType(module)->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, self->analyzer.GetUniqueFunctionName(), module);
        Wide::Semantic::CodegenContext::EmitFunctionBody(thunk_function, arguments, [this](Wide::Semantic::CodegenContext& con) {
            assert(initializers);
            for (auto&&init : *initializers)
                init->GetValue(con);
            con->CreateRetVoid();
        });
    }
    WideFunctionType* GetFunctionType() {
        auto&& a = self->analyzer;
        return a.GetFunctionType(a.GetVoidType(), arguments, false);
    }
    Wide::Util::optional<std::vector<std::shared_ptr<Expression>>> initializers;
    Callable* base_callable;
    llvm::Function* thunk_function;
    UserDefinedType* self;
    Type* target_base;
    std::vector<Type*> arguments;
};
struct Wide::Semantic::UserDefinedType::ImportConstructorResolvable : OverloadResolvable {
    ImportConstructorResolvable(Type* base, OverloadSet* set, UserDefinedType* self)
        : base(base), imported_set(set), self(self) {}
    Type* base;
    OverloadSet* imported_set;
    UserDefinedType* self;
    std::unordered_map<std::vector<Type*>, std::unique_ptr<ImportConstructorCallable>, Wide::Semantic::VectorTypeHasher> ImportedConstructorThunks;
    Wide::Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final {
        // Adjust the first argument back up because we need a real self ref.
        if (args.size() == 2)
            if (args[1]->Decay()->IsDerivedFrom(base) == Type::InheritanceRelationship::UnambiguouslyDerived || args[1]->Decay() == base)
                return Wide::Util::none;
        auto import = imported_set->ResolveWithArguments(args, source);
        if (import.first) {
            import.second[0] = self->analyzer.GetLvalueType(self);
            return import.second;
        }
        return Wide::Util::none;
    }
    Callable* GetCallableForResolution(std::vector<Type*> args, Type* source, Analyzer& a) override final {
        auto import = imported_set->ResolveWithArguments(args, source);
        import.second[0] = self->analyzer.GetLvalueType(self);
        if (ImportedConstructorThunks.find(args) == ImportedConstructorThunks.end())
            ImportedConstructorThunks[args] = Wide::Memory::MakeUnique<ImportConstructorCallable>(import.first, self, base, import.second);
        return ImportedConstructorThunks.at(args).get();
    }
};

OverloadSet* UserDefinedType::CreateConstructorOverloadSet(Parse::Access access) {
    auto user_defined = [&, this] {
        if (type->constructor_decls.empty())
            return analyzer.GetOverloadSet();
        std::unordered_set<OverloadResolvable*> resolvables;
        for (auto&&f : type->constructor_decls) {
            if (f.first <= access)
                for (auto&& func : f.second)
                    resolvables.insert(analyzer.GetCallableForFunction(analyzer.GetWideFunction(func.get(), this, "type", this, GetNonstaticLookup(func.get(), "type"))));
        }
        return analyzer.GetOverloadSet(resolvables, GetContext());
    };
    auto user_defined_constructors = user_defined();
    if (access == Parse::Access::Private)
        access = Parse::Access::Protected;
    OverloadSet* Imported = analyzer.GetOverloadSet();
    for (auto& pair : GetMemberData().imported_constructors) {
        if (!pair.second) {
            pair.second = Wide::Memory::MakeUnique<ImportConstructorResolvable>(pair.first, pair.first->GetConstructorOverloadSet(access), this);
        }
        Imported = analyzer.GetOverloadSet(Imported, analyzer.GetOverloadSet(pair.second.get()));
    }
    return analyzer.GetOverloadSet(Imported, analyzer.GetOverloadSet(user_defined_constructors, GetDefaultData().SimpleConstructors));
}

namespace {
    struct ImportAssignmentResolvable : OverloadResolvable {
        ImportAssignmentResolvable(Type* base, OverloadSet* set)
            : base(base), imported_set(set) {}
        Type* base;
        OverloadSet* imported_set;
        Wide::Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final {
            if (args.size() == 2)
                if (args[1]->Decay()->IsDerivedFrom(base) == Type::InheritanceRelationship::UnambiguouslyDerived || args[1]->Decay() == base)
                    return Wide::Util::none;
            if (imported_set->ResolveWithArguments(args, source).first)
                return imported_set->ResolveWithArguments(args, source).second;
            return Wide::Util::none;
        }
        Callable* GetCallableForResolution(std::vector<Type*> args, Type* source, Analyzer& a) override final {
            return imported_set->Resolve(args, source);
        }
    };
}

OverloadSet* UserDefinedType::CreateOperatorOverloadSet(Parse::OperatorName name, Parse::Access access, OperatorAccess kind) {
    auto user_defined = [&, this] {
        OverloadSet* imports = analyzer.GetOverloadSet();
        if (GetMemberData().BaseImports.find(name) != GetMemberData().BaseImports.end()) {
            for (auto&&base : GetMemberData().BaseImports.at(name)) {
                if (IsDerivedFrom(base.first) != InheritanceRelationship::UnambiguouslyDerived) {
                    if (FunctionImportErrors.find(name) == FunctionImportErrors.end()
                     || FunctionImportErrors[name].find(base.first) == FunctionImportErrors[name].end())
                        FunctionImportErrors[name][base.first] = Wide::Memory::MakeUnique<SpecificError<ImportNotUnambiguousBase>>(analyzer, base.second, "Import type was not an unambiguous base.");
                    continue;
                }
                auto base_set = base.first->AccessMember(name, access == Parse::Access::Private ? Parse::Access::Protected : access, kind);
                if (name == Parse::OperatorName{ &Lexer::TokenTypes::Assignment }) {
                    if (AssignmentOperatorImportResolvables.find(base.first) == AssignmentOperatorImportResolvables.end()) {
                        AssignmentOperatorImportResolvables[base.first] = Wide::Memory::MakeUnique<ImportAssignmentResolvable>(base.first, base_set);
                    }
                    imports = analyzer.GetOverloadSet(imports, analyzer.GetOverloadSet(AssignmentOperatorImportResolvables[base.first].get()));
                } else
                    imports = analyzer.GetOverloadSet(base_set, imports);
            }
        }
        std::unordered_set<OverloadResolvable*> resolvable;
        if (type->nonvariables.find(name) != type->nonvariables.end()) {
            if (auto&& set = boost::get<Parse::OverloadSet<std::unique_ptr<Parse::Function>>>(&type->nonvariables.at(name))) {
                for (auto&& f : *set) {
                    if (f.first <= access)
                        for (auto&& func : f.second)
                            resolvable.insert(analyzer.GetCallableForFunction(analyzer.GetWideFunction(func.get(), this, GetNameAsString(name), this, GetNonstaticLookup(func.get(), name))));
                }
            }
        }
        return analyzer.GetOverloadSet(imports, analyzer.GetOverloadSet(resolvable));
    };
    if (name.size() == 1 && name.front() == &Lexer::TokenTypes::Assignment)
        return analyzer.GetOverloadSet(user_defined(), GetDefaultData().SimpleAssOps);
    if (type->nonvariables.find(name) != type->nonvariables.end())
        return analyzer.GetOverloadSet(user_defined(), AggregateType::CreateOperatorOverloadSet(name, access, kind));
    // Search base classes.
    OverloadSet* BaseOverloadSet = analyzer.GetOverloadSet();
    for (auto&&base : GetBases())
        BaseOverloadSet = analyzer.GetOverloadSet(BaseOverloadSet, base->AccessMember(name, access, kind));
    if (BaseOverloadSet != analyzer.GetOverloadSet())
        return BaseOverloadSet;
    return AggregateType::CreateOperatorOverloadSet(name, access, kind);
}

std::function<void(CodegenContext&)> UserDefinedType::BuildDestruction(Expression::InstanceKey key, std::shared_ptr<Expression> self, Context c, bool devirtualize) {
    if (type->destructor_decl) {
        std::unordered_set<OverloadResolvable*> resolvables;
        resolvables.insert(analyzer.GetCallableForFunction(analyzer.GetWideFunction(type->destructor_decl.get(), this, "~type", this, GetNonstaticLookup(type->destructor_decl.get(), "~type"))));
        auto desset = analyzer.GetOverloadSet(resolvables, analyzer.GetLvalueType(this));
        auto callable = dynamic_cast<Wide::Semantic::Function*>(desset->Resolve({ analyzer.GetLvalueType(this) }, c.from));
        auto call = callable->Call(key, { self }, { this, c.where });
        if (HasVirtualDestructor() && !devirtualize) 
            return [=](CodegenContext& c) {
                call->GetValue(c);
            };
        return [=](CodegenContext& c) {
            c->CreateCall(callable->EmitCode(c.module), self->GetValue(c));
        };
    }
    return AggregateType::BuildDestruction(key, self, c, true);    
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
    if (type->nonvariables.find("operator=") != type->nonvariables.end())
        return Type::IsCopyAssignable(access);
    return AggregateType::IsCopyAssignable(access);
}
bool UserDefinedType::IsMoveAssignable(Parse::Access access) {
    if (type->nonvariables.find("operator=") != type->nonvariables.end())
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
std::function<llvm::Function*(llvm::Module*)> UserDefinedType::CreateDestructorFunction() {
    if (!type->destructor_decl) return AggregateType::CreateDestructorFunction();
    auto desfunc = analyzer.GetWideFunction(GetWideFunction(type->destructor_decl.get(), "~type"));
    desfunc->ComputeBody();
    return[=](llvm::Module* mod) {
        return desfunc->EmitCode(mod);
    };
}

std::pair<FunctionType*, std::function<llvm::Function*(llvm::Module*)>> UserDefinedType::VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry) {
    if (auto&& special = boost::get<VTableLayout::SpecialMember>(&entry.func)) {
        auto desfunc = GetDestructorFunction();
        return { analyzer.GetFunctionType(analyzer.GetVoidType(), { analyzer.GetLvalueType(this) }, false), [=](llvm::Module* mod) { return desfunc(mod); } };
    }
    auto vfunc = boost::get<VTableLayout::VirtualFunction>(entry.func);
    auto name = vfunc.name;
    if (type->nonvariables.find(name) == type->nonvariables.end()) {
        // Could have been inherited from our primary base, if we have one.
        if (GetPrimaryBase()) {
            return GetPrimaryBase()->VirtualEntryFor(entry);
        }
        return {};
    }
    if (!boost::get<Parse::OverloadSet<std::unique_ptr<Parse::Function>>>(&type->nonvariables.at(name))) return{};
    auto&& set = boost::get<Parse::OverloadSet<std::unique_ptr<Parse::Function>>>(type->nonvariables.at(name));
    for (auto&& access : set) {
        for (auto&& func : access.second) {
            if (func->deleted) continue;

            auto widefunc = analyzer.GetWideFunction(GetWideFunction(func.get(), GetNameAsString(name)));
            if (!FunctionType::CanThunkFromFirstToSecond(entry.type, widefunc->GetSignature(), this, true))
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
    return analyzer.GetFunctionType(analyzer.GetIntegralType(32, false), {}, true);
}

std::vector<std::pair<Type*, unsigned>> UserDefinedType::GetBasesAndOffsets() {
    std::vector<std::pair<Type*, unsigned>> out;
    for (unsigned i = 0; i < GetBaseData().bases.size(); ++i) {
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
std::function<llvm::Constant*(llvm::Module*)> UserDefinedType::GetRTTI() {
    // If we have a Clang type, then use it for compat.
    if (GetVtableLayout().layout.empty()) {
        if (auto&& clangty = GetClangType(*analyzer.GetAggregateTU())) {
            return [clangty, this](llvm::Module* module) {
                return analyzer.GetAggregateTU()->GetItaniumRTTI(*clangty, module);
            };
        }
    }
    if (GetBases().size() == 0) {
        return AggregateType::GetRTTI();
    }
    if (GetBases().size() == 1) {
        auto basertti = GetBases()[0]->GetRTTI();
        return[this, basertti](llvm::Module* module) {
            std::stringstream stream;
            stream << "struct.__" << this;
            if (auto&& existing = module->getGlobalVariable(stream.str() + "_rtti")) {
                return existing;
            }
            auto mangledname = GetGlobalString(stream.str(), module);
            auto vtable_name_of_rtti = "_ZTVN10__cxxabiv120__si_class_type_infoE";
            auto vtable = module->getOrInsertGlobal(vtable_name_of_rtti, llvm::Type::getInt8PtrTy(module->getContext()));
            vtable = llvm::ConstantExpr::getInBoundsGetElementPtr(vtable, llvm::ConstantInt::get(llvm::Type::getInt8Ty(module->getContext()), 2));
            vtable = llvm::ConstantExpr::getBitCast(vtable, llvm::Type::getInt8PtrTy(module->getContext()));
            std::vector<llvm::Constant*> inits = { vtable, mangledname, basertti(module) };
            auto ty = llvm::ConstantStruct::getTypeForElements(inits);
            auto rtti = new llvm::GlobalVariable(*module, ty, true, llvm::GlobalValue::LinkageTypes::LinkOnceODRLinkage, llvm::ConstantStruct::get(ty, inits), stream.str() + "_rtti");
            return rtti;
        };
    }
    // Multiple bases. Yay.
    std::vector<std::pair<std::function<llvm::Constant*(llvm::Module*)>, unsigned>> basedata;
    for (auto&&base : GetBasesAndOffsets())
        basedata.push_back({ base.first->GetRTTI(), base.second });
    return [basedata, this](llvm::Module* module) {
        std::stringstream stream;
        stream << "struct.__" << this;
        if (auto&& existing = module->getGlobalVariable(stream.str() + "_rtti")) {
            return existing;
        }
        auto mangledname = GetGlobalString(stream.str(), module);
        auto vtable_name_of_rtti = "_ZTVN10__cxxabiv121__vmi_class_type_infoE";
        auto vtable = module->getOrInsertGlobal(vtable_name_of_rtti, llvm::Type::getInt8PtrTy(module->getContext()));
        vtable = llvm::ConstantExpr::getInBoundsGetElementPtr(vtable, llvm::ConstantInt::get(llvm::Type::getInt8Ty(module->getContext()), 2));
        vtable = llvm::ConstantExpr::getBitCast(vtable, llvm::Type::getInt8PtrTy(module->getContext()));
        std::vector<llvm::Constant*> inits = { vtable, mangledname };

        // Add flags.
        inits.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(module->getContext()), 0));

        // Add base count
        inits.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(module->getContext()), GetBases().size()));

        // Add one entry for every base.
        for (auto&&bases : basedata) {
            inits.push_back(bases.first(module));
            unsigned flags = 0x2 | (bases.second << 8);
            inits.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(module->getContext()), flags));
        }

        auto ty = llvm::ConstantStruct::getTypeForElements(inits);
        auto rtti = new llvm::GlobalVariable(*module, ty, true, llvm::GlobalValue::LinkageTypes::LinkOnceODRLinkage, llvm::ConstantStruct::get(ty, inits), stream.str() + "_rtti");
        return rtti;
    };
}
bool UserDefinedType::HasDeclaredDynamicFunctions() {
    for (auto&& nonvar : type->nonvariables)
        if (auto&& overset = boost::get<Parse::OverloadSet<std::unique_ptr<Parse::Function>>>(&nonvar.second))
           for (auto&& access : *overset)
               for (auto&& func : access.second)
                   if (func->dynamic)
                       return true;

    if (type->destructor_decl)
        return type->destructor_decl->dynamic;
    return false;
}
bool UserDefinedType::IsConstant() {
    return !type->destructor_decl && AggregateType::IsConstant();
}
Wide::Util::optional<std::pair<unsigned, Lexer::Range>> UserDefinedType::SizeOverride() {
    for (auto&& attr : type->attributes) {
        if (auto&& ident = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
            if (auto&& string = boost::get<std::string>(&ident->val)) {
                if (*string == "size") {
                    auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
                    if (!dynamic_cast<IntegralType*>(expr->GetType(Expression::NoInstance()))) {
                        SizeOverrideError = Wide::Memory::MakeUnique<SpecificError<SizeOverrideNotInteger>>(analyzer, attr.initializer->location, "Alignment override not an integer.");
                        return Util::none;
                    }
                    if (!expr->IsConstant(Expression::NoInstance())) {
                        SizeOverrideError = Wide::Memory::MakeUnique<SpecificError<SizeOverrideNotConstant>>(analyzer, attr.initializer->location, "Alignment override not constant.");
                        return Util::none;
                    }
                    return std::pair<unsigned, Lexer::Range>{ analyzer.EvaluateConstantIntegerExpression(expr, Expression::NoInstance()).getLimitedValue(), attr.initializer->location };
                }
            }
        }
    }
    return Util::none;
}
Wide::Util::optional<std::pair<unsigned, Lexer::Range>> UserDefinedType::AlignOverride() {
    for (auto&& attr : type->attributes) {
        if (auto&& ident = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
            if (auto&& string = boost::get<std::string>(&ident->val)) {
                if (*string == "alignment") {
                    auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
                    if (!dynamic_cast<IntegralType*>(expr->GetType(Expression::NoInstance()))) {
                        AlignOverrideError = Wide::Memory::MakeUnique<SpecificError<AlignmentOverrideNotInteger>>(analyzer, attr.initializer->location, "Alignment override not an integer.");
                        return Util::none;
                    }
                    if (!expr->IsConstant(Expression::NoInstance())) {
                        AlignOverrideError = Wide::Memory::MakeUnique<SpecificError<AlignmentOverrideNotConstant>>(analyzer, attr.initializer->location, "Alignment override not constant.");
                        return Util::none;
                    }
                    return std::pair<unsigned, Lexer::Range>{ analyzer.EvaluateConstantIntegerExpression(expr, Expression::NoInstance()).getLimitedValue(), attr.initializer->location };
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
        for (auto&& f : type->constructor_decls) {
            for (auto&& func : f.second)
                resolvables.insert(analyzer.GetCallableForFunction(analyzer.GetWideFunction(func.get(), this, "type", this, GetNonstaticLookup(func.get(), "type"))));
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

std::shared_ptr<Expression> UserDefinedType::AccessStaticMember(std::string name, Context c) {
    auto spec = GetAccessSpecifier(c.from, this);
    if (GetMemberData().member_indices.find(name) != GetMemberData().member_indices.end()) {
        return nullptr;
    }
    if (type->nonvariables.find(name) != type->nonvariables.end()) {
        auto nonvar = boost::get<Parse::OverloadSet<std::unique_ptr<Parse::Function>>>(&type->nonvariables.at(name));
        if (nonvar) {
            std::unordered_set<OverloadResolvable*> resolvables;
            for (auto&& access : *nonvar) {
                if (spec >= access.first)
                    for (auto&& func : access.second)
                        resolvables.insert(analyzer.GetCallableForFunction(analyzer.GetWideFunction(func.get(), this, GetNameAsString(name), this, GetNonstaticLookup(func.get(), name))));
            }
            if (!resolvables.empty())
                return analyzer.GetOverloadSet(resolvables, nullptr)->BuildValueConstruction(Expression::NoInstance(), {}, c);
        }
    }
    // Any of our bases have this member?
    Type* BaseType = nullptr;
    OverloadSet* BaseOverloadSet = nullptr;
    for (auto&&base : GetBaseData().bases) {
        if (auto&& member = base->AccessStaticMember(name, c)) {
            // If there's nothing there, we win.
            // If we're an OS and the existing is an OS, we win by unifying.
            // Else we lose.
            auto otheros = dynamic_cast<OverloadSet*>(member->GetType(Expression::NoInstance())->Decay());
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
            throw SpecificError<AmbiguousMemberLookup>(analyzer, c.where, "Member lookup was ambiguous.");
        }
    }
    if (BaseOverloadSet)
        return BaseOverloadSet->BuildValueConstruction(Expression::NoInstance(), {}, c);
    if (!BaseType)
        return nullptr;
    return BaseType->AccessStaticMember(name, c);
}
bool UserDefinedType::IsFinal() {
    for (auto&& attr : type->attributes) {
        if (auto&& name = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
            if (auto&& str = boost::get<std::string>(&name->val)) {
                if (*str == "final") {
                    auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
                    if (auto&& constant = dynamic_cast<Semantic::Boolean*>(expr.get())) {
                        return constant->b;
                    }
                }
            }
        }
    }
    return false;
}
std::string UserDefinedType::GetExportBody() {
    GetExportData().exported = true;
    std::string import = "[size := " + std::to_string(size()) + "]\n";
    import += "[alignment := " + std::to_string(alignment()) + "]\n";
    import += "[__always_keep_in_memory := true]\n";
    import += "[__llvm_name := \"" + GetLLVMTypeName() + "\"]\n";
    import += "type " + explain() + "{\n";
    for (auto&&member : GetMemberData().member_indices) {
        // Need lvalue/rvalue/value overloads.
        auto&& var = type->variables[member.second];
        if (IsFinal() && var.access == Parse::Access::Protected) continue;
        if (var.access != Parse::Access::Public) continue;
        import += "private:\n";
        auto refname = analyzer.GetUniqueFunctionName();
        import += "[import_name := \"" + refname + "\"]\n";
        import += "get_" + member.first + "(this := " + explain() + ".rvalue) := " + analyzer.GetTypeExport(Semantic::CollapseType(analyzer.GetRvalueType(this), GetMemberData().members[member.second])) + " {}\n";
        import += "[import_name := \"" + refname + "\"]\n";
        import += "get_" + member.first + "(this := " + explain() + ".lvalue) := " + analyzer.GetTypeExport(Semantic::CollapseType(analyzer.GetLvalueType(this), GetMemberData().members[member.second])) + " {}\n";
        auto valname = analyzer.GetUniqueFunctionName();
        import += "[import_name := \"" + valname + "\"]\n";
        import += "get_" + member.first + "(this := " + explain() + ") := " + analyzer.GetTypeExport(GetMemberData().members[member.second]) + " {}\n";
        import += "public:\n";
        import += "using " + member.first + " := " + "get_" + member.first + "();\n";
        GetExportData().MemberPropertyNames[member.first] = { refname, valname };
    }
    if (!GetDefaultData().IsComplex) {
        // Make sure that all exported implicitly generated functions are prepared.
        AggregateType::PrepareExportedFunctions(GetDefaultData().AggregateOps, GetDefaultData().AggregateCons, !type->destructor_decl);
        if (GetDefaultData().AggregateCons.copy_constructor) {
            import += "public:\n";
            import += "[import_name := \"" + GetSpecialFunctionName(SpecialFunction::CopyConstructor) + "\"]\n";
            import += "type(other := " + explain() + ".lvalue) {}\n";
        }
        if (GetDefaultData().AggregateCons.move_constructor) {
            import += "public:\n";
            import += "[import_name := \"" + GetSpecialFunctionName(SpecialFunction::MoveConstructor) + "\"]\n";
            import += "type(other := " + explain() + ".rvalue) {}\n";
        }
        if (GetDefaultData().AggregateCons.default_constructor) {
            import += "public:\n";
            import += "[import_name := \"" + GetSpecialFunctionName(SpecialFunction::DefaultConstructor) + "\"]\n";
            import += "type() {}\n";
        }
        if (GetDefaultData().AggregateOps.copy_operator) {
            import += "public:\n";
            import += "[import_name := \"" + GetSpecialFunctionName(SpecialFunction::CopyAssignmentOperator) + "\"]\n";
            import += "operator=(other := " + explain() + ".lvalue) {}\n";
        }
        if (GetDefaultData().AggregateOps.move_operator) {
            import += "public:\n";
            import += "[import_name := \"" + GetSpecialFunctionName(SpecialFunction::MoveAssignmentOperator) + "\"]\n";
            import += "operator=(other := " + explain() + ".rvalue) {}\n";
        }
        import += "public:\n";
        import += "[import_name := \"" + GetSpecialFunctionName(SpecialFunction::Destructor) + "\"]\n";
        import += "~type() {}\n";
    }
    import += "}";
    return import;
}
void UserDefinedType::Export(llvm::Module* mod) {
    for (auto&&tuple : GetExportData().MemberPropertyNames) {
        auto memtype = GetMemberData().members[GetMemberData().member_indices[tuple.first]];
        auto fty = analyzer.GetFunctionType(Semantic::CollapseType(analyzer.GetLvalueType(this), memtype), { analyzer.GetLvalueType(this) }, false);
        llvm::Function* reffunc = llvm::Function::Create(llvm::cast<llvm::FunctionType>(fty->GetLLVMType(mod)->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, tuple.second.first, mod);
        auto location = GetLocation(GetMemberData().member_indices[tuple.first] + type->bases.size());
        CodegenContext::EmitFunctionBody(reffunc, { analyzer.GetLvalueType(this) }, [&](CodegenContext& con) {
            auto val = con->GetInsertBlock()->getParent()->arg_begin();
            if (auto&& field = boost::get<Wide::Semantic::LLVMFieldIndex>(&location)) {
                con->CreateRet(con->CreateStructGEP(val, field->index));
                return;
            } 
            auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
            self = con->CreateConstGEP1_32(self, boost::get<Wide::Semantic::EmptyBaseOffset>(location).offset);
            con->CreateRet(con->CreatePointerCast(self, memtype->GetLLVMType(con)->getPointerTo()));
        });
        fty = analyzer.GetFunctionType(memtype, { this }, false);
        llvm::Function* valfunc = llvm::Function::Create(llvm::cast<llvm::FunctionType>(fty->GetLLVMType(mod)->getElementType()), llvm::GlobalValue::LinkageTypes::ExternalLinkage, tuple.second.second, mod);
        auto complexmem = CreatePrimGlobal(Range::Empty(), analyzer.GetLvalueType(memtype), [valfunc](CodegenContext& con) { return valfunc->arg_begin(); });
        auto self = CreatePrimGlobal(Range::Empty(), this, [valfunc](CodegenContext& con) { return ++valfunc->arg_begin(); });
        auto val = PrimitiveAccessMember(self, GetMemberData().member_indices[tuple.first] + type->bases.size());
        auto inplace = memtype->BuildInplaceConstruction(Expression::NoInstance(), complexmem, { val }, { this, Wide::Lexer::Range(std::make_shared<std::string>("Analyzer internal function")) });
        CodegenContext::EmitFunctionBody(valfunc, { this }, [&](CodegenContext& con) {
            if (AlwaysKeepInMemory(mod)) {
                if (memtype->AlwaysKeepInMemory(mod)) {
                    inplace->GetValue(con);
                    con->CreateRetVoid();
                    return;
                }
                con->CreateRet(con->CreateLoad(con->CreateCall(reffunc, con->GetInsertBlock()->getParent()->arg_begin())));
                return;
            }
            if (auto&& field = boost::get<Wide::Semantic::LLVMFieldIndex>(&location)) {
                con->CreateRet(con->CreateExtractValue(con->GetInsertBlock()->getParent()->arg_begin(), { field->index }));
                return;
            }
            con->CreateRet(llvm::UndefValue::get(memtype->GetLLVMType(con)));
        });
    }
    // Emit any and all aggregate functions that need emitting.
    AggregateType::Export(mod);
}
bool UserDefinedType::AlwaysKeepInMemory(llvm::Module* mod) {
    if (GetExportData().exported)
        return true;
    for (auto&& attr : type->attributes) {
        if (auto&& name = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
            if (auto&& str = boost::get<std::string>(&name->val)) {
                if (*str == "__always_keep_in_memory") {
                    auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
                    if (auto&& constant = dynamic_cast<Semantic::Boolean*>(expr.get())) {
                        return constant->b;
                    }
                }
            }
        }
    }
    return AggregateType::AlwaysKeepInMemory(mod);
}
std::string UserDefinedType::GetLLVMTypeName() {
    for (auto&& attr : type->attributes) {
        if (auto&& name = dynamic_cast<const Parse::Identifier*>(attr.initialized.get())) {
            if (auto&& str = boost::get<std::string>(&name->val)) {
                if (*str == "__llvm_name") {
                    auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer.get(), [](Parse::Name, Lexer::Range) { return nullptr; });
                    if (auto&& constant = dynamic_cast<Semantic::String*>(expr.get())) {
                        return constant->str;
                    }
                }
            }
        }
    }
    std::stringstream strstr;
    strstr << this;
    return "__" + strstr.str();
}