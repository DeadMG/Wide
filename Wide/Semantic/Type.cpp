#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Lexer/Token.h>
#include <Wide/Semantic/UserDefinedType.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Util/DebugUtilities.h>
#include <sstream>

using namespace Wide;
using namespace Semantic;

#pragma warning(push, 0)
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <clang/AST/AST.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

Type* Type::GetContext(Analyzer& a) {
    return a.GetGlobalModule();
}
clang::QualType Type::GetClangType(ClangTU& TU, Analyzer& a) {
    throw std::runtime_error("This type has no Clang representation.");
}

ConcreteExpression ConcreteExpression::BuildIncrement(bool b, Context c) {
    return t->BuildIncrement(*this, b, c);
}

ConcreteExpression ConcreteExpression::BuildNegate(Context c) {
    return t->BuildNegate(*this, c);
}

ConcreteExpression ConcreteExpression::BuildCall(ConcreteExpression l, ConcreteExpression r, Context c) {
    std::vector<ConcreteExpression> exprs;
    exprs.push_back(l);
    exprs.push_back(r);
    return BuildCall(std::move(exprs), c);
}

Util::optional<ConcreteExpression> ConcreteExpression::PointerAccessMember(std::string mem, Context c) {
    return t->PointerAccessMember(*this, std::move(mem), c);
}

ConcreteExpression ConcreteExpression::BuildDereference(Context c) {
    return t->BuildDereference(*this, c);
}

ConcreteExpression ConcreteExpression::BuildCall(ConcreteExpression lhs, Context c) {
    std::vector<ConcreteExpression> exprs;
    exprs.push_back(lhs);
    return BuildCall(std::move(exprs), c);
}

ConcreteExpression ConcreteExpression::AddressOf(Context c) {
    return t->AddressOf(*this, c);
}

Codegen::Expression* ConcreteExpression::BuildBooleanConversion(Context c) {
    return t->BuildBooleanConversion(*this, c);
}

ConcreteExpression ConcreteExpression::BuildBinaryExpression(ConcreteExpression other, Lexer::TokenType ty, Context c) {
    return t->BuildBinaryExpression(*this, other, ty, c);
}

ConcreteExpression ConcreteExpression::BuildBinaryExpression(ConcreteExpression rhs, Lexer::TokenType type, std::vector<ConcreteExpression> destructors, Context c) {
    return t->BuildBinaryExpression(*this, rhs, std::move(destructors), type, c);
}

ConcreteExpression ConcreteExpression::BuildMetaCall(std::vector<ConcreteExpression> args, Context c) {
    return t->BuildMetaCall(*this, std::move(args), c);
}
ConcreteExpression ConcreteExpression::BuildCall(std::vector<ConcreteExpression> arguments, std::vector<ConcreteExpression> destructors, Context c) {
    return t->BuildCall(*this, std::move(arguments), std::move(destructors), c);
}

Wide::Util::optional<ConcreteExpression> ConcreteExpression::AccessMember(std::string name, Context c) {
    return t->AccessMember(*this, std::move(name), c);
}

ConcreteExpression ConcreteExpression::BuildValue(Context c) {
    return t->BuildValue(*this, c);
}

ConcreteExpression ConcreteExpression::BuildCall(std::vector<ConcreteExpression> args, Context c) {
    return t->BuildCall(*this, std::move(args), c);
}

ConcreteExpression ConcreteExpression::BuildCall(Context c) {
    return BuildCall(std::vector<ConcreteExpression>(), c);
}

ConcreteExpression Type::BuildValueConstruction(ConcreteExpression arg, Context c) {
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildValueConstruction(std::move(args), c);
}
ConcreteExpression Type::BuildRvalueConstruction(ConcreteExpression arg, Context c) {
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildRvalueConstruction(std::move(args), c);
}
ConcreteExpression Type::BuildLvalueConstruction(ConcreteExpression arg, Context c) {
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildLvalueConstruction(std::move(args), c);
}
ConcreteExpression Type::BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) {
    if (IsReference())
        return Decay()->BuildCall(val, std::move(args), c);
    throw std::runtime_error("Attempted to call a type that did not support it.");
}
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, ConcreteExpression arg, Context c){
    std::vector<ConcreteExpression> args;
    args.push_back(arg);
    return BuildInplaceConstruction(mem, std::move(args), c);
}
ConcreteExpression Type::BuildValueConstruction(Context c) {
    std::vector<ConcreteExpression> args;
    return BuildValueConstruction(args, c);
}
ConcreteExpression Type::BuildRvalueConstruction(Context c) {
    std::vector<ConcreteExpression> args;
    return BuildRvalueConstruction(args, c);
}
ConcreteExpression Type::BuildLvalueConstruction(Context c) {
    std::vector<ConcreteExpression> args;
    return BuildLvalueConstruction(args, c);
}
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, Context c) {
    std::vector<ConcreteExpression> args;
    return BuildInplaceConstruction(mem, args, c);
}
Wide::Util::optional<ConcreteExpression> Type::AccessMember(ConcreteExpression e, std::string name, Context c) {
    if (IsReference())
        return Decay()->AccessMember(e, std::move(name), c);
    return Wide::Util::none;
}
ConcreteExpression Type::BuildValue(ConcreteExpression lhs, Context c) {
    if (IsComplexType(*c))
        throw std::runtime_error("Internal Compiler Error: Attempted to build a complex type into a register.");
    if (lhs.t->IsReference())
        return ConcreteExpression(lhs.t->Decay(), c->gen->CreateLoad(lhs.Expr));
    return lhs;
}            
ConcreteExpression Type::BuildNegate(ConcreteExpression val, Context c) {
    if (IsReference())
        return Decay()->BuildNegate(val, c);
    return ConcreteExpression(c->GetBooleanType(), c->gen->CreateNegateExpression(val.BuildBooleanConversion(c)));
}

static const std::unordered_map<Lexer::TokenType, Lexer::TokenType> Assign = []() -> std::unordered_map<Lexer::TokenType, Lexer::TokenType> {
    std::unordered_map<Lexer::TokenType, Lexer::TokenType> assign;
    assign[Lexer::TokenType::LeftShift] = Lexer::TokenType::LeftShiftAssign;
    assign[Lexer::TokenType::RightShift] = Lexer::TokenType::RightShiftAssign;
    assign[Lexer::TokenType::Minus] = Lexer::TokenType::MinusAssign;
    assign[Lexer::TokenType::Plus] = Lexer::TokenType::PlusAssign;
    assign[Lexer::TokenType::Or] = Lexer::TokenType::OrAssign;
    assign[Lexer::TokenType::And] = Lexer::TokenType::AndAssign;
    assign[Lexer::TokenType::Xor] = Lexer::TokenType::XorAssign;
    assign[Lexer::TokenType::Dereference] = Lexer::TokenType::MulAssign;
    assign[Lexer::TokenType::Modulo] = Lexer::TokenType::ModAssign;
    assign[Lexer::TokenType::Divide] = Lexer::TokenType::DivAssign;
    return assign;
}();

OverloadSet* ConcreteExpression::AccessMember(Lexer::TokenType name, Context c) {
    return t->Decay()->AccessMember(t, name, *c);
}

ConcreteExpression Type::BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, std::vector<ConcreteExpression> destructors, Lexer::TokenType type, Context c) {
    for(auto x : destructors)
        c(x);
    return BuildBinaryExpression(lhs, rhs, type, c);
}

ConcreteExpression Type::BuildBinaryExpression(ConcreteExpression lhs, ConcreteExpression rhs, Lexer::TokenType type, Context c) {
    if (IsReference())
        return Decay()->BuildBinaryExpression(lhs, rhs, type, c);

    // If this function is entered, it's because the type-specific logic could not resolve the operator.
    // So let us attempt ADL.
    auto adlset = c->GetOverloadSet(lhs.AccessMember(type, c), c->GetOverloadSet(lhs.t->PerformADL(type, lhs.t, rhs.t, *c), rhs.t->PerformADL(type, lhs.t, rhs.t, *c)));
    std::vector<Type*> arguments;
    arguments.push_back(lhs.t);
    arguments.push_back(rhs.t);
    if (auto call = adlset->Resolve(arguments, *c)) {
        return call->Call(lhs, rhs, c);
    }
    
    // ADL has failed to find us a suitable operator, so fall back to defaults.
    // First implement binary op in terms of op=
    if (Assign.find(type) != Assign.end()) {
        auto lval = BuildLvalueConstruction(lhs, c).BuildBinaryExpression(rhs, Assign.at(type), c);
        lval.t = c->GetRvalueType(lval.t);
        return lval;
    }

    // Try fallbacks for relational operators
    // And default assignment for non-complex types.        
    switch(type) {
    case Lexer::TokenType::NotEqCmp:
        return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::EqCmp, c).BuildNegate(c);
    case Lexer::TokenType::EqCmp:
        return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::LT, c).BuildNegate(c).BuildBinaryExpression(rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, c).BuildNegate(c), Lexer::TokenType::And, c);
    case Lexer::TokenType::LTE:
        return rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, c).BuildNegate(c);
    case Lexer::TokenType::GT:
        return rhs.BuildBinaryExpression(lhs, Lexer::TokenType::LT, c);
    case Lexer::TokenType::GTE:
        return lhs.BuildBinaryExpression(rhs, Lexer::TokenType::LT, c).BuildNegate(c);
    //case Lexer::TokenType::Assignment:
    //    if (!IsComplexType() && lhs.t->Decay() == rhs.t->Decay() && IsLvalueType(lhs.t))
    //        return ConcreteExpression(lhs.t, c->gen->CreateStore(lhs.Expr, rhs.BuildValue(c).Expr));
    //    break;
    case Lexer::TokenType::Or:
        return ConcreteExpression(c->GetBooleanType(), lhs.BuildBooleanConversion(c)).BuildBinaryExpression(ConcreteExpression(c->GetBooleanType(), rhs.BuildBooleanConversion(c)), Wide::Lexer::TokenType::Or, c);
    case Lexer::TokenType::And:
        return ConcreteExpression(c->GetBooleanType(), lhs.BuildBooleanConversion(c)).BuildBinaryExpression(ConcreteExpression(c->GetBooleanType(), rhs.BuildBooleanConversion(c)), Wide::Lexer::TokenType::And, c);
    }
    throw std::runtime_error("Attempted to build a binary expression; but it could not be found by the type, and a default could not be applied.");
}

ConcreteExpression Type::BuildRvalueConstruction(std::vector<ConcreteExpression> args, Context c) {
    Codegen::Expression* mem = c->gen->CreateVariable(GetLLVMType(*c), alignment(*c));
    mem = c->gen->CreateChainExpression(BuildInplaceConstruction(mem, args, c), mem);
    ConcreteExpression out(c->GetRvalueType(this), mem);
    c(out);
    out.steal = true;
    return out;
}

void Context::operator()(ConcreteExpression e) {
    if (RAIIHandler)
        RAIIHandler(e);
}

ConcreteExpression Type::BuildLvalueConstruction(std::vector<ConcreteExpression> args, Context c) {
    Codegen::Expression* mem = c->gen->CreateVariable(GetLLVMType(*c), alignment(*c));
    mem = c->gen->CreateChainExpression(BuildInplaceConstruction(mem, args, c), mem);
    ConcreteExpression out(c->GetLvalueType(this), mem);
    c(out);
    out.steal = true;
    return out;
}            
Codegen::Expression* Type::BuildInplaceConstruction(Codegen::Expression* mem, std::vector<ConcreteExpression> args, Context c) {
    std::vector<Type*> types;
    args.insert(args.begin(), ConcreteExpression(c->GetLvalueType(this), mem));
    for (auto arg : args)
        types.push_back(arg.t);
    auto set = GetConstructorOverloadSet(*c);
    auto call = set->Resolve(types, *c);
    if (!call)
        throw std::runtime_error("Attempted to construct a type, but no constructor could be called.");
    return c->gen->CreateChainExpression(call->Call(std::move(args), c).Expr, mem);
}

ConcreteExpression Type::BuildValueConstruction(std::vector<ConcreteExpression> args, Context c) {
    if (IsComplexType(*c))
        throw std::runtime_error("Internal compiler error: Attempted to value construct a complex UDT.");
    if (args.size() == 1 && args[0].t == this)
        return args[0];
    auto mem = c->gen->CreateVariable(GetLLVMType(*c), alignment(*c));
    return ConcreteExpression(this, c->gen->CreateLoad(c->gen->CreateChainExpression(BuildInplaceConstruction(mem, std::move(args), c), mem)));
}
ConcreteExpression Type::AddressOf(ConcreteExpression obj, Context c) {
    // TODO: Remove this restriction, it is not very Wide.
    if (!IsLvalueType(obj.t))
        throw std::runtime_error("Attempted to take the address of something that was not an lvalue.");
    return ConcreteExpression(c->GetPointerType(obj.t->Decay()), obj.Expr);
}

OverloadSet* Type::CreateADLOverloadSet(Lexer::TokenType what, Type* lhs, Type* rhs, Analyzer& a) {
    if (IsReference())
        return Decay()->CreateADLOverloadSet(what, lhs, rhs, a);
    auto context = GetContext(a);
    if (!context)
        return a.GetOverloadSet();
    return GetContext(a)->AccessMember(GetContext(a), what, a);
}

OverloadSet* Type::PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Analyzer& a) {
    if (IsReference())
        return Decay()->PerformADL(what, lhs, rhs, a);
    if (ADLResults.find(lhs) != ADLResults.end()) {
        if (ADLResults[lhs].find(rhs) != ADLResults[lhs].end())
            if (ADLResults[lhs][rhs].find(what) != ADLResults[lhs][rhs].end())
                return ADLResults[lhs][rhs][what];
    }
    return ADLResults[lhs][rhs][what] = CreateADLOverloadSet(what, lhs, rhs, a);
}

OverloadSet* Type::CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Analyzer& a) {
    if (IsReference())
        return Decay()->AccessMember(t, type, a);
    return a.GetOverloadSet();
}

OverloadSet* Type::AccessMember(Type* t, Lexer::TokenType type, Analyzer& a) {
    if (this->OperatorOverloadSets.find(t) != OperatorOverloadSets.end())
        if (OperatorOverloadSets[t].find(type) != OperatorOverloadSets[t].end())
            return OperatorOverloadSets[t][type];
    return OperatorOverloadSets[t][type] = CreateOperatorOverloadSet(t, type, a);
}

std::size_t MetaType::size(Analyzer& a) { return a.gen->GetInt8AllocSize(); }
std::size_t MetaType::alignment(Analyzer& a) { return a.gen->GetDataLayout().getABIIntegerTypeAlignment(8); }

std::function<llvm::Type*(llvm::Module*)> MetaType::GetLLVMType(Analyzer& a) {
    std::stringstream typenam;
    typenam << this;
    auto nam = typenam.str();
    return [=](llvm::Module* mod) -> llvm::Type* {
        if (mod->getTypeByName(nam))
            return mod->getTypeByName(nam);
        return llvm::StructType::create(nam, llvm::IntegerType::getInt8Ty(mod->getContext()), nullptr);
    };
}
bool Type::IsA(Type* self, Type* other, Analyzer& a) {
    return
        other == self ||
        other == a.GetRvalueType(self) ||
        self == a.GetLvalueType(other) && other->IsCopyConstructible(a) ||
        self == a.GetRvalueType(other) && other->IsMoveConstructible(a);
}

#pragma warning(disable : 4800)

bool Type::IsMoveConstructible(Analyzer& a) {
    auto set = GetConstructorOverloadSet(a);
    std::vector<Type*> arguments;
    arguments.push_back(a.GetLvalueType(this));
    arguments.push_back(a.GetRvalueType(this));
    return set->Resolve(std::move(arguments), a);
}

bool Type::IsCopyConstructible(Analyzer& a) {
    // A Clang struct with a deleted copy constructor can be both noncomplex and non-copyable at the same time.
    auto set = GetConstructorOverloadSet(a);
    std::vector<Type*> arguments;
    arguments.push_back(a.GetLvalueType(this));
    arguments.push_back(a.GetLvalueType(this));
    return set->Resolve(std::move(arguments), a);
}

bool Type::IsCopyAssignable(Analyzer& a) {
    auto set = AccessMember(a.GetLvalueType(this), Lexer::TokenType::Assignment, a);
    std::vector<Type*> arguments;
    arguments.push_back(a.GetLvalueType(this));
    arguments.push_back(a.GetLvalueType(this));
    return set->Resolve(std::move(arguments), a);
}

bool Type::IsMoveAssignable(Analyzer& a) {
    auto set = AccessMember(a.GetLvalueType(this), Lexer::TokenType::Assignment, a);
    std::vector<Type*> arguments;
    arguments.push_back(a.GetLvalueType(this));
    arguments.push_back(a.GetRvalueType(this));
    return set->Resolve(std::move(arguments), a);
}

Type* MetaType::GetConstantContext(Wide::Semantic::Analyzer& a) {
    return this;
}

OverloadSet* PrimitiveType::CreateOperatorOverloadSet(Type* t, Lexer::TokenType what, Analyzer& a) {
    if (what != Lexer::TokenType::Assignment)
        return Type::CreateOperatorOverloadSet(t, what, a);

    if (t != a.GetLvalueType(this))
        return a.GetOverloadSet();
        
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    types.push_back(this);
    return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
        return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, args[1].Expr));
    }, types, a));
}

OverloadSet* PrimitiveType::CreateConstructorOverloadSet(Analyzer& a) {
    std::unordered_set<OverloadResolvable*> callables;
    auto construct_from_ref = [](std::vector<ConcreteExpression> args, Context c) {
        return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, c->gen->CreateLoad(args[1].Expr)));
    };
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    types.push_back(a.GetLvalueType(this));
    callables.insert(make_resolvable(construct_from_ref, types, a));
    types[1] = a.GetRvalueType(this);
    callables.insert(make_resolvable(construct_from_ref, types, a));
    return a.GetOverloadSet(callables);
}

OverloadSet* MetaType::CreateConstructorOverloadSet(Analyzer& a) {
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    auto default_constructor = make_resolvable([](std::vector<ConcreteExpression> args, Context){ return args[0]; }, types, a);
    return a.GetOverloadSet(PrimitiveType::CreateConstructorOverloadSet(a), a.GetOverloadSet(default_constructor));
}

ConcreteExpression Callable::Call(Context c) {
    return Call(std::vector<ConcreteExpression>(), c);
}
ConcreteExpression Callable::Call(ConcreteExpression arg, Context c) {
    std::vector<ConcreteExpression> e;
    e.push_back(arg);
    return Call(e, c);
}
ConcreteExpression Callable::Call(ConcreteExpression arg1, ConcreteExpression arg2, Context c) {
    std::vector<ConcreteExpression> e;
    e.push_back(arg1);
    e.push_back(arg2);
    return Call(e, c);
}

ConcreteExpression Callable::Call(std::vector<ConcreteExpression> args, Context c) {
    for (auto&& arg : args) {
        if (!arg.t->IsReference()) {
            auto mem = c->gen->CreateVariable(arg.t->GetLLVMType(*c), arg.t->alignment(*c));
            auto store = c->gen->CreateStore(mem, arg.Expr);
            arg = ConcreteExpression(c->GetRvalueType(arg.t), c->gen->CreateChainExpression(store, mem));
        }
    }
    return CallFunction(AdjustArguments(std::move(args), c), c);
}

std::vector<ConcreteExpression> Semantic::AdjustArgumentsForTypes(std::vector<ConcreteExpression> args, std::vector<Type*> types, Context c) {
    if (args.size() != types.size())
        Wide::Util::DebugBreak();
    std::vector<ConcreteExpression> out;
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (!args[i].t->IsA(args[i].t, types[i], *c))
            Wide::Util::DebugBreak();
        out.push_back(types[i]->BuildValueConstruction(args[i], c));
    }
    return out;
}

OverloadSet* Type::CreateDestructorOverloadSet(Analyzer& a) {
    struct DestructNothing : OverloadResolvable, Callable {
        unsigned GetArgumentCount() override final { return 1; }
        Type* MatchParameter(Type* t, unsigned num, Analyzer& a) override final { assert(num == 0); return t; }
        Callable* GetCallableForResolution(std::vector<Type*> tys, Analyzer& a) override final { return this; }
        std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
        ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final { return args[0]; }
    };
    return a.GetOverloadSet(a.arena.Allocate<DestructNothing>());
}
struct assign : OverloadResolvable, Callable {
    std::vector<Type*> types;
    std::function<ConcreteExpression(std::vector<ConcreteExpression>, Context)> action;

    assign(std::vector<Type*> tys, std::function<ConcreteExpression(std::vector<ConcreteExpression>, Context)> func)
        : types(tys), action(std::move(func)) {}

    unsigned GetArgumentCount() override final { return types.size(); }
    Type* MatchParameter(Type* t, unsigned num, Analyzer& a) override final {
        if (t->IsA(t, types[num], a))
            return types[num];
        return nullptr;
    }
    Callable* GetCallableForResolution(std::vector<Type*> tys, Analyzer& a) override final {
        assert(types == tys);
        return this;
    }
    ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
        assert(types.size() == args.size());
        for (std::size_t num = 0; num < types.size(); ++num)
            assert(types[num] == args[num].t);
        return action(args, c);
    }
    std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final {
        return AdjustArgumentsForTypes(args, types, c);
    }
};
OverloadResolvable* Semantic::make_resolvable(std::function<ConcreteExpression(std::vector<ConcreteExpression>, Context)> f, std::vector<Type*> types, Analyzer& a) {
    return a.arena.Allocate<assign>(types, std::move(f));
}
OverloadSet* PrimitiveType::PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Analyzer& a) {
    return Type::PerformADL(what, lhs->Decay(), rhs->Decay(), a);
}
OverloadSet* TupleInitializable::CreateConstructorOverloadSet(Analyzer& a) {
    struct TupleConstructor : public OverloadResolvable, Callable {
        TupleConstructor(TupleInitializable* p) : self(p) {}
        TupleInitializable* self;
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
            // This stuff won't be called unless we are valid
            auto tupty = dynamic_cast<TupleType*>(args[1].t->Decay());
            assert(tupty);
            auto members_opt = self->GetTypesForTuple(*c);
            assert(members_opt);
            auto members = *members_opt;

            if (members.size() == 0)
                return args[0];
            Codegen::Expression* p = nullptr;
            for (std::size_t i = 0; i < members.size(); ++i) {
                auto memory = self->PrimitiveAccessMember(args[0], i, *c);
                auto argument = tupty->PrimitiveAccessMember(args[1], i, *c);
                auto expr = members[i]->BuildInplaceConstruction(memory.Expr, argument, c);
                p = p ? c->gen->CreateChainExpression(p, expr) : expr;
            }
            return ConcreteExpression(c->GetLvalueType(self), c->gen->CreateChainExpression(p, args[0].Expr));
        }
    };
    return a.GetOverloadSet(a.arena.Allocate<TupleConstructor>(this));
}