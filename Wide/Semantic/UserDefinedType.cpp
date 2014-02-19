#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/ClangTU.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/ConstructorType.h>
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

std::vector<Type*> GetTypesFromType(const AST::Type* t, Analyzer& a, Type* context) {
    std::vector<Type*> out;
    for (auto expr : t->bases) {
        auto base = a.AnalyzeExpression(context, expr, [](Wide::Semantic::ConcreteExpression) { assert(false); });
        auto con = dynamic_cast<ConstructorType*>(base.t->Decay());
        if (!con) throw std::runtime_error("Attempted to define base classes but an expression was not a type.");
        auto udt = dynamic_cast<BaseType*>(con->GetConstructedType());
        if (!udt) throw std::runtime_error("Attempted to inherit from a type but that type was not a user-defined type.");
        out.push_back(udt);
    }
    for (auto&& var : t->variables) {
        auto expr = a.AnalyzeExpression(context, var.first->initializer, [](ConcreteExpression e) {});
        expr.t = expr.t->Decay();
        if (auto con = dynamic_cast<ConstructorType*>(expr.t))
            out.push_back(con->GetConstructedType());
        else
            throw std::runtime_error("No longer support in-class member initializers.");
    }
    return out;
}

UserDefinedType::UserDefinedType(const AST::Type* t, Analyzer& a, Type* higher)
: AggregateType(GetTypesFromType(t, a, higher), a)
, context(higher)
, type(t){
    unsigned i = 0;
    for (auto&& var : type->variables)
        members[var.first->name.front()] = i++ + type->bases.size();
}

std::vector<UserDefinedType::member> UserDefinedType::GetMembers() {
    std::vector<UserDefinedType::member> out;
    for (unsigned i = 0; i < type->bases.size(); ++i) {
        member m;
        m.t = AggregateType::GetMembers()[i];
        m.name = dynamic_cast<AST::Identifier*>(type->bases[i])->val;
        m.num = AggregateType::GetFieldIndex(i);
        out.push_back(m);
    }
    for (unsigned i = 0; i < type->variables.size(); ++i) {
        member m;
        m.t = AggregateType::GetMembers()[i + type->bases.size()];
        m.name = type->variables[i].first->name.front();
        m.num = AggregateType::GetFieldIndex(i) + type->bases.size();
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
                resolvables.insert(c->GetCallableForFunction(f, self.t));
        }
        if (!resolvables.empty())
            return c->GetOverloadSet(resolvables, self.t)->BuildValueConstruction({ self }, c);
    }
    // Any of our bases have this member?
    Wide::Util::optional<ConcreteExpression> result;
    for (std::size_t i = 0; i < AggregateType::GetMembers().size() - type->variables.size(); ++i) {
        auto base = PrimitiveAccessMember(expr, i, *c);
        if (auto member = base.AccessMember(name, c)) {
            // If there's nothing there, we win.
            // If we're an OS and the existing is an OS, we win by unifying.
            // Else we lose.
            if (!result) { result = member; continue; }  
            auto os = dynamic_cast<OverloadSet*>(result->t->Decay());
            auto otheros = dynamic_cast<OverloadSet*>(member->t->Decay());
            if (!os || !otheros) throw std::runtime_error("Attempted to access a member, but it was ambiguous in more than one base class.");
            result = c->GetOverloadSet(os, otheros, self.t)->BuildValueConstruction({ self }, c);
        }
    }
    return result;
}
clang::QualType UserDefinedType::GetClangType(ClangTU& TU, Analyzer& a) {
    if (clangtypes.find(&TU) != clangtypes.end())
        return clangtypes[&TU];
    
    std::stringstream stream;
    stream << "__" << this;

    auto recdecl = clang::CXXRecordDecl::Create(TU.GetASTContext(), clang::TagDecl::TagKind::TTK_Struct, TU.GetDeclContext(), clang::SourceLocation(), clang::SourceLocation(), TU.GetIdentifierInfo(stream.str()));
    recdecl->startDefinition();
    clangtypes[&TU] = TU.GetASTContext().getTypeDeclType(recdecl);

    for (std::size_t i = 0; i < type->variables.size(); ++i) {
        auto var = clang::FieldDecl::Create(
            TU.GetASTContext(),
            recdecl,
            clang::SourceLocation(),
            clang::SourceLocation(),
            TU.GetIdentifierInfo(type->variables[i].first->name.front()),
            AggregateType::GetMembers()[i]->GetClangType(TU, a),
            nullptr,
            nullptr,
            false,
            clang::InClassInitStyle::ICIS_NoInit
            );
        var->setAccess(clang::AccessSpecifier::AS_public);
        recdecl->addDecl(var);
    }
    // Todo: Expose member functions
    // Only those which are not generic right now
    if (type->Functions.find("()") != type->Functions.end()) {
        for(auto&& x : type->Functions.at("()")->functions) {
            bool skip = false;
            for(auto&& arg : x->args)
                if (!arg.type)
                    skip = true;
            if (skip) continue;
            auto f = a.GetWideFunction(x, this);
            auto sig = f->GetSignature(a);
            auto ret = sig->GetReturnType();
            auto args = sig->GetArguments();
            args.erase(args.begin());
            sig = a.GetFunctionType(ret, args);
            auto meth = clang::CXXMethodDecl::Create(
                TU.GetASTContext(), 
                recdecl, 
                clang::SourceLocation(), 
                clang::DeclarationNameInfo(TU.GetASTContext().DeclarationNames.getCXXOperatorName(clang::OverloadedOperatorKind::OO_Call), clang::SourceLocation()),
                sig->GetClangType(TU, a),
                0,
                clang::FunctionDecl::StorageClass::SC_Extern,
                false,
                false,
                clang::SourceLocation()
            );      
            assert(!meth->isStatic());
            meth->setAccess(clang::AccessSpecifier::AS_public);
            std::vector<clang::ParmVarDecl*> decls;
            for(auto&& arg : sig->GetArguments()) {
                decls.push_back(clang::ParmVarDecl::Create(TU.GetASTContext(),
                    meth,
                    clang::SourceLocation(),
                    clang::SourceLocation(),
                    nullptr,
                    arg->GetClangType(TU, a),
                    nullptr,
                    clang::VarDecl::StorageClass::SC_Auto,
                    nullptr
                ));
            }
            meth->setParams(decls);
            recdecl->addDecl(meth);
            if (clangtypes.empty()) {
                auto trampoline = a.gen->CreateFunction([=, &a, &TU](llvm::Module* m) -> llvm::Type* {
                    auto fty = llvm::dyn_cast<llvm::FunctionType>(sig->GetLLVMType(a)(m)->getPointerElementType());
                    std::vector<llvm::Type*> args;
                    for(auto it = fty->param_begin(); it != fty->param_end(); ++it) {
                        args.push_back(*it);
                    }
                    // If T is complex, then "this" is the second argument. Else it is the first.
                    auto self = TU.GetLLVMTypeFromClangType(TU.GetASTContext().getTypeDeclType(recdecl), a)(m)->getPointerTo();
                    if (sig->GetReturnType()->IsComplexType(a)) {
                        args.insert(args.begin() + 1, self);
                    } else {
                        args.insert(args.begin(), self);
                    }
                    return llvm::FunctionType::get(fty->getReturnType(), args, false)->getPointerTo();
                }, TU.MangleName(meth), nullptr, true);// If an i8/i1 mismatch, fix it up for us amongst other things.
                // The only statement is return f().
                std::vector<Codegen::Expression*> exprs;
                // Unlike OverloadSet, we wish to simply forward all parameters after ABI adjustment performed by FunctionCall in Codegen.
                if (sig->GetReturnType()->IsComplexType(a)) {
                    // Two hidden arguments: ret, this, skip this and do the rest.
                    for(std::size_t i = 0; i < sig->GetArguments().size() + 2; ++i) {
                        exprs.push_back(a.gen->CreateParameterExpression(i));
                    }
                } else {
                    // One hidden argument: this, pos 0.
                    for(std::size_t i = 0; i < sig->GetArguments().size() + 1; ++i) {
                        exprs.push_back(a.gen->CreateParameterExpression(i));
                    }
                }
                trampoline->AddStatement(a.gen->CreateReturn(a.gen->CreateFunctionCall(a.gen->CreateFunctionValue(f->GetName()), exprs, f->GetLLVMType(a))));
            }
        }
    }

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
        IsBinaryComplex = IsBinaryComplex || a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this))->Resolve(copytypes, a, this);
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
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this))->Resolve(copytypes, a, this);

        std::vector<Type*> movetypes;
        movetypes.push_back(a.GetLvalueType(this));
        movetypes.push_back(a.GetRvalueType(this));
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this))->Resolve(movetypes, a, this);

        std::vector<Type*> defaulttypes;
        defaulttypes.push_back(a.GetLvalueType(this));
        HasDefaultConstructor = a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this))->Resolve(defaulttypes, a, this);
    }
    if (type->opcondecls.find(Lexer::TokenType::Assignment) != type->opcondecls.end()) {
        std::vector<Type*> copytypes;
        copytypes.push_back(a.GetLvalueType(this));
        copytypes.push_back(copytypes.front());
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->opcondecls.at(Lexer::TokenType::Assignment), a.GetLvalueType(this))->Resolve(copytypes, a, this);

        std::vector<Type*> movetypes;
        movetypes.push_back(a.GetLvalueType(this));
        movetypes.push_back(a.GetRvalueType(this));
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->opcondecls.at(Lexer::TokenType::Assignment), a.GetLvalueType(this))->Resolve(movetypes, a, this);
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
                resolvables.insert(a.GetCallableForFunction(f, a.GetLvalueType(this)));
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
        return a.GetOverloadSet(user_defined_constructors, AggregateType::CreateNondefaultConstructorOverloadSet(a));
    return a.GetOverloadSet(user_defined_constructors, AggregateType::CreateConstructorOverloadSet(a, access));
}

OverloadSet* UserDefinedType::CreateOperatorOverloadSet(Type* self, Lexer::TokenType name, Lexer::Access access, Analyzer& a) {
    auto user_defined = [&, this] {
        if (type->opcondecls.find(name) != type->opcondecls.end()) {
            std::unordered_set<OverloadResolvable*> resolvable;
            for (auto&& f : type->opcondecls.at(name)->functions) {
                if (f->access <= access)
                    resolvable.insert(a.GetCallableForFunction(f, self));
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

            unsigned GetArgumentCount() override final { return 1; }
            Type* MatchParameter(Type* t, unsigned num, Analyzer& a, Type* source) override final { assert(num == 0); return t; }
            Callable* GetCallableForResolution(std::vector<Type*> tys, Analyzer& a) override final { return this; }
            std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
            ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final { 
                std::vector<Type*> types;
                types.push_back(args[0].t);
                auto userdestructor = c->GetOverloadSet(self->type->Functions.at("~"), args[0].t)->Resolve(types, *c, self)->Call({ args[0] }, c).Expr;
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
    return AggregateType::GetMembers();
}

InheritanceRelationship UserDefinedType::IsDerivedFrom(Type* other, Analyzer& a) {
    auto base = dynamic_cast<BaseType*>(other);
    if (!base) return InheritanceRelationship::NotDerived;
    InheritanceRelationship result = InheritanceRelationship::NotDerived;
    for (std::size_t i = 0; i < type->bases.size(); ++i) {
        auto ourbase = dynamic_cast<BaseType*>(AggregateType::GetMembers()[i]);
        assert(ourbase);
        if (ourbase == other) {
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
    // It is unambiguous so just hit on the first base we come across
    for (std::size_t i = 0; i < type->bases.size(); ++i) {
        auto base = dynamic_cast<BaseType*>(AggregateType::GetMembers()[i]);
        if (base == other)
            return a.gen->CreateFieldExpression(current, GetFieldIndex(i));
        if (base->IsDerivedFrom(other, a) == InheritanceRelationship::UnambiguouslyDerived)
            return base->AccessBase(other, a.gen->CreateFieldExpression(current, GetFieldIndex(i)), a);
    }
    assert(false);
    return nullptr;
    // shush warning
}

ConcreteExpression UserDefinedType::PrimitiveAccessMember(ConcreteExpression e, unsigned num, Analyzer& a) {
    return AggregateType::PrimitiveAccessMember(e, num, a);
}