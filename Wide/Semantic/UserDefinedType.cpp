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

void AddAllBases(std::unordered_set<Type*>& all_bases, Type* root) {
    all_bases.insert(root);
    for (auto base : root->GetBases()) {
        all_bases.insert(base);
        AddAllBases(all_bases, base);
    }
}
UserDefinedType::BaseData::BaseData(UserDefinedType* self) {
    for (auto expr : self->type->bases) {
        auto base = self->analyzer.AnalyzeExpression(self->context, expr);
        auto con = dynamic_cast<ConstructorType*>(base->GetType()->Decay());
        if (!con) throw NotAType(base->GetType(), expr->location);
        // Should be UDT or ClangType right now.
        auto udt = (Type*)dynamic_cast<UserDefinedType*>(con->GetConstructedType());
        if (udt == self) throw InvalidBase(con->GetConstructedType(), expr->location);
        if (!udt) udt = dynamic_cast<ClangType*>(con->GetConstructedType());
        if (!udt) throw InvalidBase(con->GetConstructedType(), expr->location);
        bases.push_back(udt);
        // 2.4.1 (b)
        if (!PrimaryBase && !udt->GetVtableLayout().layout.empty())
            PrimaryBase = udt;
    }
}

UserDefinedType::VTableData::VTableData(UserDefinedType* self) {
    // If we have a primary base, our primary vtable starts with theirs.
    if (self->GetPrimaryBase()) funcs = self->GetPrimaryBase()->GetPrimaryVTable();
    std::unordered_set<Type*> all_bases;
    for (auto&& base : self->GetBases())
        AddAllBases(all_bases, base);

    std::unordered_map<const Parse::DynamicFunction*, unsigned> primary_dynamic_functions;
    std::unordered_map<const Parse::DynamicFunction*, VTableLayout::VirtualFunctionEntry> dynamic_functions;
    unsigned index = 0;
    for (auto overset : self->type->functions) {
        for (auto pair : overset.second) {
            for (auto func : pair.second) {
                // The function has to be not explicitly marked dynamic *and* not dynamic by any base class.
                if (IsMultiTyped(func)) continue;
                if (func->deleted) continue;

                std::vector<Type*> arguments;
                if (func->args.size() == 0 || func->args.front().name != "this")
                    arguments.push_back(self->analyzer.GetLvalueType(self));
                for (auto arg : func->args) {
                    auto ty = self->analyzer.AnalyzeCachedExpression(self->context, arg.type)->GetType()->Decay();
                    auto con_type = dynamic_cast<ConstructorType*>(ty);
                    if (!con_type)
                        throw Wide::Semantic::NotAType(ty, arg.location);
                    arguments.push_back(con_type->GetConstructedType());
                }

                for (auto base : all_bases) {
                    auto base_layout = base->GetPrimaryVTable();
                    for (unsigned i = 0; i < base_layout.layout.size(); ++i) {
                        if (auto vfunc = boost::get<VTableLayout::VirtualFunction>(&base_layout.layout[i].function)) {
                            if (vfunc->name != overset.first) continue;
                            // The this arguments will be different, but they should have the same category of lvalue/rvalue.
                            if (arguments.size() != vfunc->args.size()) continue;
                            for (std::size_t argi = 1; argi < arguments.size(); ++argi) {
                                if (vfunc->args[argi] != arguments[argi])
                                    continue;
                            }
                            // Score- it's inherited.
                            if (base == self->GetPrimaryBase())
                                primary_dynamic_functions[func] = i;
                            else {
                                auto vfunc = VTableLayout::VirtualFunction{ overset.first, arguments, self->analyzer.GetWideFunction(func, self, arguments, overset.first)->GetSignature()->GetReturnType() };
                                dynamic_functions[func] = { func->abstract, vfunc };
                            }
                        }
                    }
                }
                if (func->dynamic){
                    auto vfunc = VTableLayout::VirtualFunction{ overset.first, arguments, self->analyzer.GetWideFunction(func, self, arguments, overset.first)->GetSignature()->GetReturnType() };
                    dynamic_functions[func] = { func->abstract, vfunc };                    
                }
            }
        }
    }
    if (self->type->destructor_decl) {
        for (auto base : all_bases) {
            for (unsigned i = 0; i < base->GetPrimaryVTable().layout.size(); ++i) {
                if (auto mem = boost::get<VTableLayout::SpecialMember>(&base->GetPrimaryVTable().layout[i].function)) {
                    if (*mem == VTableLayout::SpecialMember::Destructor) {
                        if (base == self->GetPrimaryBase())
                            primary_dynamic_functions[self->type->destructor_decl] = i;
                        else
                            dynamic_functions[self->type->destructor_decl] = { false, VTableLayout::SpecialMember::Destructor };
                    }
                }
            }
        }
        if (self->type->destructor_decl->dynamic)
            dynamic_functions[self->type->destructor_decl] = { false, VTableLayout::SpecialMember::Destructor };
    }
    // If we don't have a primary base but we do have dynamic functions, first add offset and RTTI
    if (!self->GetPrimaryBase() && !dynamic_functions.empty()) {
        funcs.offset = 2;
        VTableLayout::VirtualFunctionEntry offset;
        VTableLayout::VirtualFunctionEntry rtti;
        offset.abstract = false;
        rtti.abstract = false;
        offset.function = VTableLayout::SpecialMember::OffsetToTop;
        rtti.function = VTableLayout::SpecialMember::RTTIPointer;
        funcs.layout.insert(funcs.layout.begin(), rtti);
        funcs.layout.insert(funcs.layout.begin(), offset);
    }

    // For every dynamic function that is not inherited from the primary base, add a slot.
    // Else, set the slot to the primary base's slot.
    for (auto func : dynamic_functions) {
        if (primary_dynamic_functions.find(func.first) == primary_dynamic_functions.end()) {
            VTableIndices[func.first] = funcs.layout.size() - funcs.offset;
            funcs.layout.push_back(func.second);
            // If we are a dynamic destructor then need to add deleting destructor too.
            if (auto specmem = boost::get<VTableLayout::SpecialMember>(&func.second.function))
                if (*specmem == VTableLayout::SpecialMember::Destructor)
                    funcs.layout.push_back(VTableLayout::VirtualFunctionEntry{ false, VTableLayout::SpecialMember::ItaniumABIDeletingDestructor });
            continue;
        }
        VTableIndices[func.first] = primary_dynamic_functions[func.first] - funcs.offset;
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
    //if (!self->GetType()->IsReference())
    //    self = BuildRvalueConstruction(Expressions(std::move(self)), { this, c.where });
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
                    resolvables.insert(analyzer.GetCallableForFunction(func, self->GetType(), name));
        }
        auto selfty = self->GetType();
        if (!resolvables.empty())
            return analyzer.GetOverloadSet(resolvables, analyzer.GetRvalueType(selfty))->BuildValueConstruction({ std::move(self) }, c);
    }
    // Any of our bases have this member?
    Type* BaseType = nullptr;
    OverloadSet* BaseOverloadSet = nullptr;
    for (auto base : GetBaseData().bases) {
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
            throw AmbiguousLookup(name, base, BaseType, c.where);
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
        std::vector<Type*> types;
        if (func->args.size() == 0 || func->args.front().name != "this")
            types.push_back(analyzer.GetLvalueType(this));
        for (auto arg : func->args) {
            auto expr = analyzer.AnalyzeCachedExpression(GetContext(), arg.type);
            if (auto ty = dynamic_cast<ConstructorType*>(expr->GetType()->Decay()))
                types.push_back(ty->GetConstructedType());
            else
                assert(false);
        }
        return types;
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
        widedes->AddExportName(TU.GetObject(des, clang::CXXDtorType::Dtor_Complete));
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
                mfunc->AddExportName(TU.GetObject(con, clang::CXXCtorType::Ctor_Complete));
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
                        auto mfunc = analyzer.GetWideFunction(func, this, types, overset.first);
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
                auto meth = clang::CXXMethodDecl::Create(
                    TU.GetASTContext(),
                    recdecl,
                    clang::SourceLocation(),
                    clang::DeclarationNameInfo(clang::DeclarationName(TU.GetIdentifierInfo(overset.first)), clang::SourceLocation()),
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
                    auto mfunc = analyzer.GetWideFunction(func, this, types, overset.first);
                    mfunc->ComputeBody();
                    mfunc->AddExportName(TU.GetObject(meth));
                }
            }
        }
    }
    recdecl->completeDefinition();
    TU.GetDeclContext()->addDecl(recdecl);
    analyzer.AddClangType(clangtypes[&TU], this);
    return clangtypes[&TU];
}

bool UserDefinedType::HasMember(std::string name) {
    return GetMemberData().member_indices.find(name) != GetMemberData().member_indices.end();
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

    if (self->type->destructor_decl && !self->type->destructor_decl->defaulted)
        IsComplex = true;

    for (auto conset : self->type->constructor_decls) {
        for (auto con : conset.second) {
            if (con->args.size() == 0) {
                AggregateCons.default_constructor = false;
                continue;
            }
            if (con->args.size() == 1 && con->args[0].name == "this") {
                AggregateCons.default_constructor = false;
                continue;
            }
            if (con->args.size() > 2) continue;
            ConstructorType* conty = nullptr;
            unsigned paramnum = 0;
            if (con->args.size() == 2) {
                if (con->args[0].name != "this") continue;
                paramnum = 1;
            }
            if (!con->args[paramnum].type) throw std::runtime_error("fuck");
            auto conobj = self->analyzer.AnalyzeExpression(self, con->args[paramnum].type);
            conty = dynamic_cast<ConstructorType*>(conobj->GetType()->Decay());
            if (!conty) throw std::runtime_error("Fuck");
            if (conty->GetConstructedType() == self->analyzer.GetLvalueType(self)) {
                IsComplex = IsComplex || !con->defaulted;
                AggregateCons.copy_constructor = false;
            } else if (conty->GetConstructedType() == self->analyzer.GetRvalueType(self)) {
                IsComplex = IsComplex || !con->defaulted;
                AggregateCons.move_constructor = false;
            }            
        }
    }

    if (self->type->functions.find("operator=") != self->type->functions.end())  {
        auto set = self->type->functions.at("operator=");
        for (auto op_access_pair : set) {
            auto ops = op_access_pair.second;
            for (auto op : ops) {
                if (op->args.size() == 0 || op->args.size() > 2) continue;
                ConstructorType* conty = nullptr;
                unsigned paramnum = 0;
                if (op->args.size() == 2) {
                    if (op->args[0].name != "this") continue;
                    paramnum = 1;
                }
                if (!op->args[paramnum].type) throw std::runtime_error("fuck");
                auto conobj = self->analyzer.AnalyzeExpression(self, op->args[paramnum].type);
                conty = dynamic_cast<ConstructorType*>(conobj->GetType()->Decay());
                if (!conty) throw std::runtime_error("Fuck");
                if (conty->GetConstructedType() == self->analyzer.GetLvalueType(self)) {
                    IsComplex = IsComplex || !op->defaulted;
                    AggregateOps.copy_operator = false;
                } else if (conty->GetConstructedType() == self->analyzer.GetRvalueType(self)) {
                    IsComplex = IsComplex || !op->defaulted;
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

OverloadSet* UserDefinedType::CreateOperatorOverloadSet(Lexer::TokenType name, Parse::Access access) {
    // So bad.
    auto funcname = "operator" + *name;
    if (*name == "(")
        funcname += ")";
    else if (*name == "[")
        funcname += "]";
    auto user_defined = [&, this] {
        if (type->functions.find(funcname) != type->functions.end()) {
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto&& f : type->functions.at(funcname)) {
                if (f.first <= access)
                    for (auto func : f.second)
                        resolvable.insert(analyzer.GetCallableForFunction(func, this, funcname));
            }
            return analyzer.GetOverloadSet(resolvable);
        }
        return analyzer.GetOverloadSet();
    };
    if (name == &Lexer::TokenTypes::Assignment)
        return analyzer.GetOverloadSet(user_defined(), GetDefaultData().SimpleAssOps);
    if (type->functions.find(funcname) != type->functions.end())
        return analyzer.GetOverloadSet(user_defined(), AggregateType::CreateOperatorOverloadSet(name, access));
    return AggregateType::CreateOperatorOverloadSet(name, access);
}

std::function<void(CodegenContext&)> UserDefinedType::BuildDestructorCall(std::shared_ptr<Expression> self, Context c, bool devirtualize) {
    if (type->destructor_decl) {
        assert(self->GetType()->IsReference(this));
        std::unordered_set<OverloadResolvable*> resolvables;
        resolvables.insert(analyzer.GetCallableForFunction(type->destructor_decl, self->GetType(), "~type"));
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
    auto desfunc = analyzer.GetWideFunction(type->destructor_decl, analyzer.GetLvalueType(this), { analyzer.GetLvalueType(this) }, "~type");
    return desfunc->EmitCode(module);
}

std::shared_ptr<Expression> UserDefinedType::VirtualEntryFor(VTableLayout::VirtualFunctionEntry entry, unsigned offset) {
    struct VTableThunk : Expression {
        VTableThunk(Function* f, unsigned off)
        : widefunc(f), offset(off) {}
        Function* widefunc;
        unsigned offset;
        Type* GetType() override final {
            return widefunc->GetSignature();
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            widefunc->EmitCode(con);
            if (offset == 0)
                return widefunc->EmitCode(con);
            auto this_index = (std::size_t)widefunc->GetSignature()->GetReturnType()->IsComplexType();
            std::stringstream strstr;
            strstr << "__" << this << offset;
            auto thunk = llvm::Function::Create(llvm::dyn_cast<llvm::FunctionType>(widefunc->GetSignature()->GetLLVMType(con)->getElementType()), llvm::GlobalValue::LinkageTypes::InternalLinkage, strstr.str(), con.module);
            llvm::BasicBlock* bb = llvm::BasicBlock::Create(con.module->getContext(), "entry", thunk);
            llvm::IRBuilder<> irbuilder(bb);
            auto self = std::next(thunk->arg_begin(), widefunc->GetSignature()->GetReturnType()->IsComplexType());
            auto offset_self = irbuilder.CreateConstGEP1_32(irbuilder.CreatePointerCast(self, llvm::IntegerType::getInt8PtrTy(con.module->getContext())), -offset);
            auto cast_self = irbuilder.CreatePointerCast(offset_self, std::next(widefunc->EmitCode(con)->arg_begin(), this_index)->getType());
            std::vector<llvm::Value*> args;
            for (std::size_t i = 0; i < thunk->arg_size(); ++i) {
                if (i == this_index)
                    args.push_back(cast_self);
                else
                    args.push_back(std::next(thunk->arg_begin(), i));
            }
            auto call = irbuilder.CreateCall(widefunc->EmitCode(con), args);
            if (call->getType() == llvm::Type::getVoidTy(con.module->getContext()))
                irbuilder.CreateRetVoid();
            else
                irbuilder.CreateRet(call);
            return thunk;
        }
    };
    if (auto special = boost::get<VTableLayout::SpecialMember>(&entry.function)) {
        if (*special == VTableLayout::SpecialMember::Destructor)
            return Wide::Memory::MakeUnique<VTableThunk>(analyzer.GetWideFunction(type->destructor_decl, analyzer.GetLvalueType(this), { analyzer.GetLvalueType(this) }, "~type"), offset);
        if (*special == VTableLayout::SpecialMember::ItaniumABIDeletingDestructor)
            return Wide::Memory::MakeUnique<VTableThunk>(analyzer.GetWideFunction(type->destructor_decl, analyzer.GetLvalueType(this), { analyzer.GetLvalueType(this) }, "~type"), offset);
    }
    auto vfunc = boost::get<VTableLayout::VirtualFunction>(entry.function);
    auto name = vfunc.name;
    auto args = vfunc.args;
    auto ret = vfunc.ret;
    if (type->functions.find(name) == type->functions.end()) {
        // Could have been inherited from our primary base, if we have one.
        if (GetPrimaryBase()) {
            VTableLayout::VirtualFunctionEntry basentry;
            basentry.abstract = false;
            basentry.function = vfunc;
            return GetPrimaryBase()->VirtualEntryFor(basentry, offset);
        }
        return nullptr;
    }
    // args includes this, which will have a different type here.
    // Pretend that it really has our type.
    if (IsLvalueType(args[0]))
        args[0] = analyzer.GetLvalueType(this);
    else
        args[0] = analyzer.GetRvalueType(this);

    for (auto access : type->functions.at(name)) {
        for (auto func : access.second) {
            if (func->deleted) continue;
            std::vector<Type*> f_args;
            if (func->args.size() == 0 || func->args.front().name != "this")
                f_args.push_back(analyzer.GetLvalueType(this));
            for (auto arg : func->args) {
                auto ty = analyzer.AnalyzeCachedExpression(GetContext(), arg.type)->GetType()->Decay();
                auto con_type = dynamic_cast<ConstructorType*>(ty);
                if (!con_type)
                    throw Wide::Semantic::NotAType(ty, arg.location);
                f_args.push_back(con_type->GetConstructedType());
            }
            if (args != f_args)
                continue;
            auto widefunc = analyzer.GetWideFunction(func, this, f_args, name);
            widefunc->ComputeBody();
            if (widefunc->GetSignature()->GetReturnType() != ret)
                throw std::runtime_error("fuck");

            return Wide::Memory::MakeUnique<VTableThunk>(widefunc, offset);
        }
    }
    return nullptr;
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
            if (ident->val == "size") {
                auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer);
                if (auto integer = dynamic_cast<Integer*>(expr->GetImplementation())) {
                    return integer->value.getLimitedValue();
                }
                throw std::runtime_error("Found size attribute but the initializing expression was not a constant integer.");
            }
        }
    }
    return Util::none;
}
Wide::Util::optional<unsigned> UserDefinedType::AlignOverride() {
    for (auto attr : type->attributes) {
        if (auto ident = dynamic_cast<const Parse::Identifier*>(attr.initialized)) {
            if (ident->val == "alignment") {
                auto expr = analyzer.AnalyzeExpression(GetContext(), attr.initializer);
                if (auto integer = dynamic_cast<Integer*>(expr->GetImplementation())) {
                    return integer->value.getLimitedValue();
                }
                throw std::runtime_error("Found size attribute but the initializing expression was not a constant integer.");
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
                resolvables.insert(analyzer.GetCallableForFunction(func, analyzer.GetLvalueType(this), "type"));
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