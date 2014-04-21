#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/PointerType.h>
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

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

// I only store MY OWN dynamic functions in my vtable.
// My base's dynamic functions can go in my base's vtable.

bool UserDefinedType::IsDynamic(Analyzer& a) {
    return GetVtableLayout(a).size() != 0;
}
UserDefinedType::UserDefinedType(const AST::Type* t, Analyzer& a, Type* higher, std::string name)
: context(higher)
, type(t)
, source_name(name) 
{
    for (auto expr : t->bases) {
        auto base = a.AnalyzeExpression(context, expr, [](Wide::Semantic::ConcreteExpression) { assert(false); });
        auto con = dynamic_cast<ConstructorType*>(base.t->Decay());
        if (!con) throw NotAType(base.t, expr->location, a);
        auto udt = dynamic_cast<BaseType*>(con->GetConstructedType());
        if (!udt) throw InvalidBase(con->GetConstructedType(), expr->location, a);
        bases.push_back(udt);
        contents.push_back(con->GetConstructedType());
    }
    for (auto&& var : t->variables) {
        members[var.first->name.front().name] = contents.size();
        auto expr = a.AnalyzeExpression(context, var.first->initializer, [](ConcreteExpression e) {});
        expr.t = expr.t->Decay();
        if (auto con = dynamic_cast<ConstructorType*>(expr.t)) {
            contents.push_back(con->GetConstructedType());
            NSDMIs.push_back(nullptr);
        } else {
            contents.push_back(expr.t->Decay());
            HasNSDMI = true;
            NSDMIs.push_back(var.first->initializer);
        }
    }
    unsigned index = 0;
    for (auto overset : type->Functions) {
        if (overset.first == "type") continue; // Do not check constructors.
        for (auto func : overset.second->functions) {
            // The function has to be not explicitly marked dynamic *and* not dynamic by any base class.
            if (IsMultiTyped(func)) continue;

            VirtualFunction f;
            f.name = overset.first;
            if (func->args.size() == 0 || func->args.front().name != "this")
                f.args.push_back(a.GetLvalueType(this));
            for (auto arg : func->args) {
                auto ty = a.AnalyzeExpression(GetContext(a), arg.type, [](ConcreteExpression) {}).t->Decay();
                auto con_type = dynamic_cast<ConstructorType*>(ty);
                if (!con_type)
                    throw Wide::Semantic::NotAType(ty, arg.location, a);
                f.args.push_back(con_type->GetConstructedType());
            }
            auto is_dynamic = [&, this] {
                if (func->dynamic) return true;
                for (auto base : bases) {
                    for (auto func : base->GetVtableLayout(a)) {
                        if (func.name != overset.first) continue;
                        // The this arguments will be different.
                        if (f.args.size() != func.args.size()) continue;
                        for (auto i = 1; i < f.args.size(); ++i) {
                            if (func.args[i] != f.args[i])
                                continue;
                        }
                        return true;
                    }
                }
                return false;
            };
            if (!is_dynamic()) continue;

            f.abstract = false;
            funcs.push_back(f);
            VTableIndices[func] = index++;
        }
    }
    if (!funcs.empty())
        contents.push_back(a.GetPointerType(a.GetFunctionType(a.GetIntegralType(32, false), {})));
}

std::vector<UserDefinedType::member> UserDefinedType::GetMembers(Analyzer& a) {
    std::vector<UserDefinedType::member> out;
    for (unsigned i = 0; i < type->bases.size(); ++i) {
        member m;
        m.t = GetContents()[i];
        m.name = dynamic_cast<AST::Identifier*>(type->bases[i])->val;
        m.num = GetFieldIndex(a, i);
        m.InClassInitializer = nullptr;
        m.vptr = false;
        out.push_back(m);
    }
    for (unsigned i = 0; i < type->variables.size(); ++i) {
        member m;
        m.t = GetContents()[i + type->bases.size()];
        m.name = type->variables[i].first->name.front().name;
        m.num = GetFieldIndex(a, i) + type->bases.size();
        m.InClassInitializer = NSDMIs[i];
        m.vptr = false;
        out.push_back(m);
    }
    if (IsDynamic(a)) {
        member m;
        m.num = GetFieldIndex(a, type->bases.size() + type->variables.size());
        m.t = GetContents()[type->bases.size() + type->variables.size()];
        m.name = "__vfptr";
        m.InClassInitializer = nullptr;
        m.vptr = true;
        out.push_back(m);
    }
    return out;
}

Wide::Util::optional<ConcreteExpression> UserDefinedType::AccessMember(ConcreteExpression expr, std::string name, Context c) {
    auto self = expr.t == this ? BuildRvalueConstruction({ expr }, c) : expr;
    auto spec = GetAccessSpecifier(c, this);
    if (members.find(name) != members.end()) {
        auto member = type->variables[members[name] - type->bases.size()];
        if (spec >= member.second)
            return PrimitiveAccessMember(expr, members[name], *c);
    }
    if (type->Functions.find(name) != type->Functions.end()) {
        std::unordered_set<OverloadResolvable*> resolvables;
        for (auto f : type->Functions.at(name)->functions) {
            if (spec >= f->access)
                resolvables.insert(c->GetCallableForFunction(f, self.t, name));
        }
        if (!resolvables.empty())
            return c->GetOverloadSet(resolvables, self.t)->BuildValueConstruction({ self }, c);
    }
    // Any of our bases have this member?
    Wide::Util::optional<ConcreteExpression> result;
    Type* basetype = nullptr;
    for (std::size_t i = 0; i < GetContents().size() - type->variables.size(); ++i) {
        auto base = PrimitiveAccessMember(expr, i, *c);
        if (auto member = base.AccessMember(name, c)) {
            // If there's nothing there, we win.
            // If we're an OS and the existing is an OS, we win by unifying.
            // Else we lose.
            if (!result) { result = member; basetype = GetContents()[i];  continue; }
            auto os = dynamic_cast<OverloadSet*>(result->t->Decay());
            auto otheros = dynamic_cast<OverloadSet*>(member->t->Decay());
            if (!os || !otheros) throw AmbiguousLookup(name, basetype, GetContents()[i], c.where, *c);
            result = c->GetOverloadSet(os, otheros, self.t)->BuildValueConstruction({ self }, c);
        }
    }
    return result;
}
Wide::Util::optional<clang::QualType> UserDefinedType::GetClangType(ClangTU& TU, Analyzer& a) {
    if (clangtypes.find(&TU) != clangtypes.end())
        return clangtypes[&TU];
    if (IsDynamic(a)) return Wide::Util::none;
    
    std::stringstream stream;
    // The name of the LLVM type is generated by AggregateType based on "this".
    // so downcast before using this to get the same name.
    // Should refactor someday
    stream << "__" << (AggregateType*)this;

    auto recdecl = clang::CXXRecordDecl::Create(TU.GetASTContext(), clang::TagDecl::TagKind::TTK_Struct, TU.GetDeclContext(), clang::SourceLocation(), clang::SourceLocation(), TU.GetIdentifierInfo(stream.str()));
    recdecl->startDefinition();
    clangtypes[&TU] = TU.GetASTContext().getTypeDeclType(recdecl);
    if (type->bases.size() != 0) return Util::none;

    for (std::size_t i = 0; i < type->variables.size(); ++i) {
        auto memberty = GetContents()[i]->GetClangType(TU, a);
        if (!memberty) return Wide::Util::none;
        auto var = clang::FieldDecl::Create(
            TU.GetASTContext(),
            recdecl,
            clang::SourceLocation(),
            clang::SourceLocation(),
            TU.GetIdentifierInfo(type->variables[i].first->name.front().name),
            *memberty,
            nullptr,
            nullptr,
            false,
            clang::InClassInitStyle::ICIS_NoInit
            );
        var->setAccess(clang::AccessSpecifier::AS_public);
        recdecl->addDecl(var);
    }
    /*for (auto overset : type->Functions) {
        for (auto func : overset.second->functions) {
            if (IsMultiTyped(func)) continue;
            std::vector<Type*> types;
            for (auto arg : func->args) {
                if (!arg.type) return;
                auto expr = a.AnalyzeExpression(GetContext(a), arg.type, [](ConcreteExpression expr) {});
                if (auto ty = dynamic_cast<ConstructorType*>(expr.t->Decay()))
                    types.push_back(ty->GetConstructedType());
                else
                    return;
            }
            auto mfunc = a.GetWideFunction(func, GetContext(a), types, overset.first);
            mfunc->ComputeBody(a);

        }
    }*/
    // Todo: Expose member functions
    // Only those which are not generic right now
    recdecl->completeDefinition();
    auto size = TU.GetASTContext().getTypeSizeInChars(TU.GetASTContext().getTypeDeclType(recdecl).getTypePtr());
    TU.GetDeclContext()->addDecl(recdecl);
    a.AddClangType(TU.GetASTContext().getTypeDeclType(recdecl), this);
    return clangtypes[&TU];
}

bool UserDefinedType::HasMember(std::string name) {
    return type->Functions.find(name) != type->Functions.end() || members.find(name) != members.end();
}

bool UserDefinedType::BinaryComplex(Analyzer& a) {
    if (BCCache)
        return *BCCache;
    bool IsBinaryComplex = AggregateType::IsComplexType(a);
    if (type->Functions.find("type") != type->Functions.end()) {
        std::vector<Type*> copytypes;
        copytypes.push_back(a.GetLvalueType(this));
        copytypes.push_back(copytypes.front());
        IsBinaryComplex = IsBinaryComplex || a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this), "type")->Resolve(copytypes, a, this);
    }
    if (type->Functions.find("~") != type->Functions.end()) 
        IsBinaryComplex = true;
    BCCache = IsBinaryComplex;
    return *BCCache;
}

#pragma warning(disable : 4800)

bool UserDefinedType::UserDefinedComplex(Analyzer& a) {
    if (UDCCache)
        return *UDCCache;
    // We are user-defined complex if the user specified any of the following:
    // Copy/move constructor, copy/move assignment operator, destructor.
    bool IsUserDefinedComplex = false;
    if (type->Functions.find("type") != type->Functions.end()) {
        std::vector<Type*> copytypes;
        copytypes.push_back(a.GetLvalueType(this));
        copytypes.push_back(copytypes.front());
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this), "type")->Resolve(copytypes, a, this);

        std::vector<Type*> movetypes;
        movetypes.push_back(a.GetLvalueType(this));
        movetypes.push_back(a.GetRvalueType(this));
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this), "type")->Resolve(movetypes, a, this);
    }
    if (type->opcondecls.find(Lexer::TokenType::Assignment) != type->opcondecls.end()) {
        std::vector<Type*> copytypes;
        copytypes.push_back(a.GetLvalueType(this));
        copytypes.push_back(copytypes.front());
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->opcondecls.at(Lexer::TokenType::Assignment), a.GetLvalueType(this), GetNameForOperator(Lexer::TokenType::Assignment))->Resolve(copytypes, a, this);

        std::vector<Type*> movetypes;
        movetypes.push_back(a.GetLvalueType(this));
        movetypes.push_back(a.GetRvalueType(this));
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->opcondecls.at(Lexer::TokenType::Assignment), a.GetLvalueType(this), GetNameForOperator(Lexer::TokenType::Assignment))->Resolve(movetypes, a, this);
    }
    if (type->Functions.find("~") != type->Functions.end())
        IsUserDefinedComplex = true;
    UDCCache = IsUserDefinedComplex;
    return *UDCCache;
}

OverloadSet* UserDefinedType::CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) {
    auto user_defined = [&, this] {
        if (type->Functions.find("type") == type->Functions.end())
            return a.GetOverloadSet();
        std::unordered_set<OverloadResolvable*> resolvables;
        for (auto f : type->Functions.at("type")->functions) {
            if (f->access <= access)
                resolvables.insert(a.GetCallableForFunction(f, a.GetLvalueType(this), "type"));
        }
        return a.GetOverloadSet(resolvables, a.GetLvalueType(this));
    };
    auto user_defined_constructors = user_defined();

    if (UserDefinedComplex(a))
        return user_defined_constructors;
    
    user_defined_constructors = a.GetOverloadSet(user_defined_constructors, TupleInitializable::CreateConstructorOverloadSet(a, access));

    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    if (auto default_constructor = user_defined_constructors->Resolve(types, a, this))
        return a.GetOverloadSet(user_defined_constructors, CreateNondefaultConstructorOverloadSet(a));
    if (HasNSDMI) {
        bool shouldgenerate = true;
        for (auto mem : GetMembers(a)) {
            // If we are not default constructible *and* we don't have an NSDMI to construct us, then we cannot generate a default constructor.
            if (mem.InClassInitializer)
                continue;
            if (!mem.t->GetConstructorOverloadSet(a, GetAccessSpecifier(this, mem.t, a))->Resolve({ a.GetLvalueType(mem.t) }, a, this))
                shouldgenerate = false;
        }
        if (!shouldgenerate)
            return a.GetOverloadSet(user_defined_constructors, CreateNondefaultConstructorOverloadSet(a));
        // Our automatically generated default constructor needs to initialize each member appropriately.
        struct DefaultConstructor : OverloadResolvable, Callable {
            DefaultConstructor(UserDefinedType* ty) : self(ty) {}
            UserDefinedType* self;

            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final {
                if (args.size() != 1) return Util::none;
                if (args[0] != a.GetLvalueType(self)) return Util::none;
                return args;
            }
            Callable* GetCallableForResolution(std::vector<Type*> tys, Analyzer& a) override final { return this; }
            std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
            ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
                Codegen::Expression* expr = nullptr;
                for (auto mem : self->GetMembers(*c)) {
                    if (mem.InClassInitializer) {
                        auto subobj = c->gen->CreateFieldExpression(args[0].Expr, mem.num);
                        auto subinit = mem.t->BuildInplaceConstruction(subobj, { c->AnalyzeExpression(self->GetContext(*c), mem.InClassInitializer, c.RAIIHandler) }, c);
                        expr = expr ? c->gen->CreateChainExpression(expr, subinit) : subinit;
                        continue;
                    }
                    auto subobj = c->gen->CreateFieldExpression(args[0].Expr, mem.num);
                    auto subinit = mem.t->BuildInplaceConstruction(subobj, {}, c);
                    expr = expr ? c->gen->CreateChainExpression(expr, subinit) : subinit;
                }
                return ConcreteExpression(c->GetLvalueType(self), expr);
            }
        };
        return a.GetOverloadSet(a.GetOverloadSet(a.arena.Allocate<DefaultConstructor>(this)), CreateNondefaultConstructorOverloadSet(a));
    }
    return a.GetOverloadSet(user_defined_constructors, AggregateType::CreateConstructorOverloadSet(a, access));
}

OverloadSet* UserDefinedType::CreateOperatorOverloadSet(Type* self, Lexer::TokenType name, Lexer::Access access, Analyzer& a) {
    auto user_defined = [&, this] {
        if (type->opcondecls.find(name) != type->opcondecls.end()) {
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto&& f : type->opcondecls.at(name)->functions) {
                if (f->access <= access)
                    resolvable.insert(a.GetCallableForFunction(f, self, GetNameForOperator(name)));
            }
            return a.GetOverloadSet(resolvable, self);
        }
        return a.GetOverloadSet();
    };

    if (name == Lexer::TokenType::Assignment) {
        if (UserDefinedComplex(a)) {
            return user_defined();
        }
    }
    if (type->opcondecls.find(name) != type->opcondecls.end())
        return a.GetOverloadSet(user_defined(), AggregateType::CreateOperatorOverloadSet(self, name, access, a));
    return AggregateType::CreateOperatorOverloadSet(self, name, access, a);
}

OverloadSet* UserDefinedType::CreateDestructorOverloadSet(Analyzer& a) {
    auto aggset = AggregateType::CreateDestructorOverloadSet(a);
    if (type->Functions.find("~") != type->Functions.end()) {
        struct Destructor : OverloadResolvable, Callable {
            Destructor(UserDefinedType* ty, OverloadSet* base) : self(ty), aggset(base) {}
            UserDefinedType* self;
            OverloadSet* aggset;

            Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final { 
                if (args.size() == 1) return args;
                return Util::none;
            }
            Callable* GetCallableForResolution(std::vector<Type*> tys, Analyzer& a) override final { return this; }
            std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
            ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final { 
                std::vector<Type*> types;
                types.push_back(args[0].t);
                auto userdestructor = c->GetOverloadSet(self->type->Functions.at("~"), args[0].t, "~type")->Resolve(types, *c, self)->Call({ args[0] }, c).Expr;
                auto autodestructor = aggset->Resolve(types, *c, self)->Call({ args[0] }, c).Expr;
                return ConcreteExpression(c->GetLvalueType(self), c->gen->CreateChainExpression(userdestructor, autodestructor));
            }
        };
        return a.GetOverloadSet(a.arena.Allocate<Destructor>(this, aggset));
    }
    return aggset;
}

// Gotta override these to respect our user-defined functions
// else aggregatetype will just assume them.
// Could just return Type:: all the time but AggregateType will be faster.
bool UserDefinedType::IsCopyConstructible(Analyzer& a, Lexer::Access access) {
    if (type->Functions.find("type") != type->Functions.end())
        return Type::IsCopyConstructible(a, access);
    return AggregateType::IsCopyConstructible(a, access);
}
bool UserDefinedType::IsMoveConstructible(Analyzer& a, Lexer::Access access) {
    if (type->Functions.find("type") != type->Functions.end())
        return Type::IsMoveConstructible(a, access);
    return AggregateType::IsMoveConstructible(a, access);
}
bool UserDefinedType::IsCopyAssignable(Analyzer& a, Lexer::Access access) {
    if (type->opcondecls.find(Lexer::TokenType::Assignment) != type->opcondecls.end())
        return Type::IsCopyAssignable(a, access);
    return AggregateType::IsCopyAssignable(a, access);
}
bool UserDefinedType::IsMoveAssignable(Analyzer& a, Lexer::Access access) {
    if (type->opcondecls.find(Lexer::TokenType::Assignment) != type->opcondecls.end())
        return Type::IsMoveAssignable(a, access);
    return AggregateType::IsMoveAssignable(a, access);
}

bool UserDefinedType::IsComplexType(Analyzer& a) {
    return BinaryComplex(a);
}

Wide::Util::optional<std::vector<Type*>> UserDefinedType::GetTypesForTuple(Analyzer& a) {
    if (UserDefinedComplex(a))
        return Wide::Util::none;
    return GetContents();
}

InheritanceRelationship UserDefinedType::IsDerivedFrom(Type* other, Analyzer& a) {
    auto base = dynamic_cast<BaseType*>(other);
    if (!base) return InheritanceRelationship::NotDerived;
    InheritanceRelationship result = InheritanceRelationship::NotDerived;
    for (std::size_t i = 0; i < type->bases.size(); ++i) {
        auto ourbase = dynamic_cast<BaseType*>(GetContents()[i]);
        assert(ourbase);
        if (ourbase == base) {
            if (result == InheritanceRelationship::NotDerived)
                result = InheritanceRelationship::UnambiguouslyDerived;
            else if (result == InheritanceRelationship::UnambiguouslyDerived)
                result = InheritanceRelationship::AmbiguouslyDerived;
            continue;
        }
        auto subresult = ourbase->IsDerivedFrom(other, a);
        if (subresult == InheritanceRelationship::AmbiguouslyDerived)
            result = InheritanceRelationship::AmbiguouslyDerived;
        if (subresult == InheritanceRelationship::UnambiguouslyDerived) {
            if (result == InheritanceRelationship::NotDerived)
                result = subresult;
            if (result == InheritanceRelationship::UnambiguouslyDerived)
                result = InheritanceRelationship::AmbiguouslyDerived;
        }
    }
    return result;
}
Codegen::Expression* UserDefinedType::AccessBase(Type* other, Codegen::Expression* current, Analyzer& a) {
    assert(IsDerivedFrom(other, a) == InheritanceRelationship::UnambiguouslyDerived);
    auto otherbase = dynamic_cast<BaseType*>(other);
    // It is unambiguous so just hit on the first base we come across
    for (std::size_t i = 0; i < type->bases.size(); ++i) {
        auto base = dynamic_cast<BaseType*>(GetContents()[i]);
        if (base == otherbase)
            return a.gen->CreateFieldExpression(current, GetFieldIndex(a, i));
        if (base->IsDerivedFrom(other, a) == InheritanceRelationship::UnambiguouslyDerived)
            return base->AccessBase(other, a.gen->CreateFieldExpression(current, GetFieldIndex(a, i)), a);
    }
    assert(false);
    return nullptr;
    // shush warning
}

// Is there really a purpose to this method?
ConcreteExpression UserDefinedType::PrimitiveAccessMember(ConcreteExpression e, unsigned num, Analyzer& a) {
    return AggregateType::PrimitiveAccessMember(e, num, a);
}

std::string UserDefinedType::explain(Analyzer& a) {
    if (context == a.GetGlobalModule())
        return source_name;
    return GetContext(a)->explain(a) + "." + source_name;
} 
std::vector<BaseType::VirtualFunction> UserDefinedType::ComputeVTableLayout(Analyzer& a) {
    for (auto vfunc : VTableIndices) {
        funcs[vfunc.second].ret = a.GetWideFunction(vfunc.first, this, funcs[vfunc.second].args, funcs[vfunc.second].name)->GetSignature(a)->GetReturnType();
    }
    return funcs;
}

Codegen::Expression* UserDefinedType::FunctionPointerFor(std::string name, std::vector<Type*> args, Type* ret, unsigned offset, Analyzer& a)  {
    if (type->Functions.find(name) == type->Functions.end())
        return nullptr;
    // args includes this, which will have a different type here.
    // Pretend that it really has our type.
    if (IsLvalueType(args[0]))
        args[0] = a.GetLvalueType(this);
    else
        args[0] = a.GetRvalueType(this);
    for (auto func : type->Functions.at(name)->functions) {
        std::vector<Type*> f_args;
        if (func->args.size() == 0 || func->args.front().name != "this")
            f_args.push_back(a.GetLvalueType(this));
        for (auto arg : func->args) {
            auto ty = a.AnalyzeExpression(GetContext(a), arg.type, [](ConcreteExpression) {}).t->Decay();
            auto con_type = dynamic_cast<ConstructorType*>(ty);
            if (!con_type)
                throw Wide::Semantic::NotAType(ty, arg.location, a);
            f_args.push_back(con_type->GetConstructedType());
        }
        auto widefunc = a.GetWideFunction(func, this, f_args, name);
        widefunc->ComputeBody(a);
        if (offset == 0) {
            if (args == f_args && ret == widefunc->GetSignature(a)->GetReturnType()) {
                return a.gen->CreateFunctionValue(widefunc->GetName());
            }
        }
    }
    return nullptr;
}

Codegen::Expression* UserDefinedType::GetVirtualPointer(Codegen::Expression* self, Analyzer& a) {
    if (GetContents().size() != type->variables.size() + type->bases.size())
        return a.gen->CreateFieldExpression(self, GetFieldIndex(a, type->variables.size() + type->bases.size()));
    return nullptr;
}

std::function<llvm::Type*(llvm::Module*)> UserDefinedType::GetVirtualPointerType(Analyzer& a) {
    return a.GetPointerType(a.GetFunctionType(a.GetIntegralType(32, false), {}))->GetLLVMType(a);
}

std::vector<std::pair<BaseType*, unsigned>> UserDefinedType::GetBases(Analyzer& a) {
    std::vector<std::pair<BaseType*, unsigned>> out;
    for (unsigned i = 0; i < type->bases.size(); ++i) {
        out.push_back(std::make_pair(bases[i], GetOffset(a, i)));
    }
    return out;
}
Callable* UserDefinedType::GetCallableForDynamicCall(const AST::Function* func, std::vector<Type*> types, std::string name, Analyzer& a) {
    if (VTableIndices.size() == 0)
        ComputeVTableLayout(a);
    struct DynamicCallable : Callable {
        std::vector<Type*> types;
        Type* ret;
        unsigned index;
        UserDefinedType* self;
        ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) {
            auto vtableptr = c->gen->CreateLoad(self->GetVirtualPointer(args[0].Expr, *c));
            // Index, then bitcast, then call.
            auto fty = c->GetFunctionType(ret, types);
            auto fptr = ConcreteExpression(fty, c->gen->CreatePointerCast(c->gen->CreateLoad(c->gen->CreatePointerIndex(vtableptr, index)), fty->GetLLVMType(*c)));
            return fptr.BuildCall(args, c);
        }
        std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) {
            return AdjustArgumentsForTypes(args, types, c);
        }
    };
    auto callable = new DynamicCallable();
    callable->types = types;
    callable->self = this;
    callable->index = VTableIndices[func];
    callable->ret = a.GetWideFunction(func, this, types, name)->GetSignature(a)->GetReturnType();
    return callable;
}