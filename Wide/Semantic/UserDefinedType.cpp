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
        auto udt = dynamic_cast<UserDefinedType*>(con->GetConstructedType());
        if (!udt) throw std::runtime_error("Attempted to inherit from a type but that type was not a user-defined type.");
        out.push_back(udt);
    }
    for (auto&& var : t->variables) {
        auto expr = a.AnalyzeExpression(context, var->initializer, [](ConcreteExpression e) {});
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
        members[var->name.front()] = i++ + type->bases.size();
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
        m.name = type->variables[i]->name.front();
        m.num = AggregateType::GetFieldIndex(i) + type->bases.size();
        out.push_back(m);
    }
    return out;
}

Wide::Util::optional<ConcreteExpression> UserDefinedType::AccessMember(ConcreteExpression expr, std::string name, Context c) {
    auto self = expr.t == this ? BuildRvalueConstruction(expr, c) : expr;
    if (members.find(name) != members.end())
        return PrimitiveAccessMember(expr, members[name], *c);
    if (type->Functions.find(name) != type->Functions.end()) {
        return c->GetOverloadSet(type->Functions.at(name), self.t)->BuildValueConstruction(self, c);
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
            result = c->GetOverloadSet(os, otheros, self.t)->BuildValueConstruction(self, c);
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
            TU.GetIdentifierInfo(type->variables[i]->name.front()),
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
        IsBinaryComplex = IsBinaryComplex || a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this))->Resolve(copytypes, a);
    }
    if (type->Functions.find("~") != type->Functions.end()) 
        IsBinaryComplex = true;
    BCCache = IsBinaryComplex;
    return *BCCache;
}

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
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this))->Resolve(copytypes, a);

        std::vector<Type*> movetypes;
        movetypes.push_back(a.GetLvalueType(this));
        movetypes.push_back(a.GetRvalueType(this));
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this))->Resolve(movetypes, a);

        std::vector<Type*> defaulttypes;
        defaulttypes.push_back(a.GetLvalueType(this));
        HasDefaultConstructor = a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this))->Resolve(defaulttypes, a);
    }
    if (type->opcondecls.find(Lexer::TokenType::Assignment) != type->opcondecls.end()) {
        std::vector<Type*> copytypes;
        copytypes.push_back(a.GetLvalueType(this));
        copytypes.push_back(copytypes.front());
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->opcondecls.at(Lexer::TokenType::Assignment), a.GetLvalueType(this))->Resolve(copytypes, a);

        std::vector<Type*> movetypes;
        movetypes.push_back(a.GetLvalueType(this));
        movetypes.push_back(a.GetRvalueType(this));
        IsUserDefinedComplex = IsUserDefinedComplex || a.GetOverloadSet(type->opcondecls.at(Lexer::TokenType::Assignment), a.GetLvalueType(this))->Resolve(movetypes, a);
    }
    if (type->Functions.find("~") != type->Functions.end())
        IsUserDefinedComplex = true;
    UDCCache = IsUserDefinedComplex;
    return *UDCCache;
}

ConcreteExpression UserDefinedType::BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) {
    auto self = val.t == this ? BuildRvalueConstruction(val, c) : val;
    if (type->opcondecls.find(Lexer::TokenType::OpenBracket) != type->opcondecls.end())
        return c->GetOverloadSet(type->opcondecls.at(Lexer::TokenType::OpenBracket), self.t)->BuildValueConstruction(self, c).BuildCall(std::move(args), c);
    throw std::runtime_error("Attempt to call a user-defined type with no operator() defined.");
}

OverloadSet* UserDefinedType::CreateConstructorOverloadSet(Analyzer& a) {
    auto user_defined_constructors = type->Functions.find("type") == type->Functions.end() ? a.GetOverloadSet() : a.GetOverloadSet(type->Functions.at("type"), a.GetLvalueType(this));
    if (UserDefinedComplex(a))
        return user_defined_constructors;
    
    struct TupleConstructor : public OverloadResolvable, Callable {
        TupleConstructor(UserDefinedType* p) : self(p) {}
        UserDefinedType* self;
        unsigned GetArgumentCount() override final { return 2; }
        Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) { return this; }
        Type* MatchParameter(Type* t, unsigned num, Analyzer& a) override final {
            if (num == 0) {
                if (t == a.GetLvalueType(self))
                    return t;
                else
                    return nullptr;
            }
            auto tup = dynamic_cast<TupleType*>(t->Decay());
            if (!tup) return nullptr;
            if (tup->IsA(t, self, a))
                return t;
            return nullptr;
        }
        std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
        ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) {
            // We should already have properly-typed memory at 0.
            // and the tuple at 1.
            auto tupty = dynamic_cast<TupleType*>(args[1].t->Decay());
            assert(tupty);
            if (self->GetMembers().size() == 0)
                return args[0];
            Codegen::Expression* p = nullptr;
            for (std::size_t i = 0; i < self->GetMembers().size(); ++i) {
                auto memory = self->PrimitiveAccessMember(args[0], i, *c);
                auto argument = tupty->PrimitiveAccessMember(args[1], i, *c);
                auto expr = self->GetMembers()[i].t->BuildInplaceConstruction(memory.Expr, argument, c);
                p = p ? c->gen->CreateChainExpression(p, expr) : expr;
            }
            return ConcreteExpression(c->GetLvalueType(self), c->gen->CreateChainExpression(p, args[0].Expr));
        }
    };
    user_defined_constructors = a.GetOverloadSet(user_defined_constructors, a.GetOverloadSet(a.arena.Allocate<TupleConstructor>(this)));

    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    if (auto default_constructor = user_defined_constructors->Resolve(types, a))
        return a.GetOverloadSet(user_defined_constructors, AggregateType::CreateNondefaultConstructorOverloadSet(a));
    return a.GetOverloadSet(user_defined_constructors, AggregateType::CreateConstructorOverloadSet(a));
}

OverloadSet* UserDefinedType::CreateOperatorOverloadSet(Type* self, Lexer::TokenType name, Analyzer& a) {
    if (name == Lexer::TokenType::Assignment) {
        if (UserDefinedComplex(a)) {
            if (type->opcondecls.find(name) != type->opcondecls.end())
                return a.GetOverloadSet(type->opcondecls.at(name), self);
            return a.GetOverloadSet();
        }
    }
    if (type->opcondecls.find(name) != type->opcondecls.end())
        return a.GetOverloadSet(a.GetOverloadSet(type->opcondecls.at(name), self), AggregateType::CreateOperatorOverloadSet(self, name, a));
    return AggregateType::CreateOperatorOverloadSet(self, name, a);
}

OverloadSet* UserDefinedType::CreateDestructorOverloadSet(Analyzer& a) {
    auto aggset = AggregateType::CreateDestructorOverloadSet(a);
    if (type->Functions.find("~") != type->Functions.end()) {
        struct Destructor : OverloadResolvable, Callable {
            Destructor(UserDefinedType* ty, OverloadSet* base) : self(ty), aggset(base) {}
            UserDefinedType* self;
            OverloadSet* aggset;

            unsigned GetArgumentCount() override final { return 1; }
            Type* MatchParameter(Type* t, unsigned num, Analyzer& a) override final { assert(num == 0); return t; }
            Callable* GetCallableForResolution(std::vector<Type*> tys, Analyzer& a) override final { return this; }
            std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
            ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final { 
                std::vector<Type*> types;
                types.push_back(args[0].t);
                auto userdestructor = c->GetOverloadSet(self->type->Functions.at("~"), args[0].t)->Resolve(types, *c)->Call(args[0], c).Expr;
                auto autodestructor = aggset->Resolve(types, *c)->Call(args[0], c).Expr;
                return ConcreteExpression(c->GetLvalueType(self), c->gen->CreateChainExpression(userdestructor, autodestructor));
            }
        };
        return a.GetOverloadSet(a.arena.Allocate<Destructor>(this, aggset));
    }
    return aggset;
}
bool UserDefinedType::IsCopyConstructible(Analyzer& a) {
    if (type->Functions.find("type") != type->Functions.end())
        return Type::IsCopyConstructible(a);
    return AggregateType::IsCopyConstructible(a);
}
bool UserDefinedType::IsMoveConstructible(Analyzer& a) {
    if (type->Functions.find("type") != type->Functions.end())
        return Type::IsMoveConstructible(a);
    return AggregateType::IsMoveConstructible(a);
}
bool UserDefinedType::IsCopyAssignable(Analyzer& a) {
    if (type->opcondecls.find(Lexer::TokenType::Assignment) != type->opcondecls.end())
        return Type::IsCopyAssignable(a);
    return AggregateType::IsCopyAssignable(a);
}
bool UserDefinedType::IsMoveAssignable(Analyzer& a) {
    if (type->opcondecls.find(Lexer::TokenType::Assignment) != type->opcondecls.end())
        return Type::IsMoveAssignable(a);
    return AggregateType::IsMoveAssignable(a);
}

bool UserDefinedType::IsComplexType(Analyzer& a) {
    return BinaryComplex(a);
}

Wide::Util::optional<std::vector<Type*>> UserDefinedType::GetTypesForTuple(Analyzer& a) {
    if (UserDefinedComplex(a))
        return Wide::Util::none;
    return AggregateType::GetMembers();
}

bool UserDefinedType::IsUnambiguouslyDerivedFrom(Type* other) {
    std::unordered_map<Type*, unsigned> basecount;
    std::function<void(UserDefinedType*)> count;
    count = [&](UserDefinedType* t) {
        for (std::size_t i = 0; i < t->type->bases.size(); ++i) {
            basecount[AggregateType::GetMembers()[i]]++;
            if (auto udt = dynamic_cast<UserDefinedType*>(AggregateType::GetMembers()[i])) {
                count(udt);
            }
        }
    };
    count(this);
    return basecount.find(other) != basecount.end() && basecount[other] == 1;
}
Codegen::Expression* UserDefinedType::AccessBase(Type* other, Codegen::Expression* current, Analyzer& a) {
    // It is unambiguous so just hit on the first base we come across
    for (std::size_t i = 0; i < type->bases.size(); ++i) {
        auto base = dynamic_cast<UserDefinedType*>(AggregateType::GetMembers()[i]);
        if (base == other)
            return a.gen->CreateFieldExpression(current, GetFieldIndex(i));
        if (base->IsUnambiguouslyDerivedFrom(other))
            return base->AccessBase(other, a.gen->CreateFieldExpression(current, GetFieldIndex(i)), a);
    }
    assert(false);
}