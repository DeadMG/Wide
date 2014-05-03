#include <Wide/Semantic/Type.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/SemanticError.h>
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

Type* Type::GetContext() {
    return analyzer.GetGlobalModule();
}

bool Type::IsReference(Type* to) { return false; }
bool Type::IsReference() { return false; }
Type* Type::Decay() { return this; }
std::function<void(llvm::Value*, Codegen::Generator& g)> Type::BuildDestructorCall() { return [](llvm::Value*, Codegen::Generator&) {}; }
Wide::Util::optional<clang::QualType> Type::GetClangType(ClangTU& TU) { return Wide::Util::none; }
bool Type::IsComplexType() { return false; }
Type::~Type() {}
Type* Type::GetConstantContext() { return nullptr; }
Wide::Util::optional<UniqueExpression> Type::AccessStaticMember(std::string name) { return Wide::Util::none; }
Wide::Util::optional<UniqueExpression> AccessMember(Type* t, std::string name, Lexer::Access) { return Wide::Util::none; }

bool Type::IsMoveConstructible(Lexer::Access access) {
    auto set = GetConstructorOverloadSet(access);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetRvalueType(this));
    return set->Resolve(std::move(arguments), this);
}

bool Type::IsCopyConstructible(Lexer::Access access) {
    // A Clang struct with a deleted copy constructor can be both noncomplex and non-copyable at the same time.
    auto set = GetConstructorOverloadSet(access);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetLvalueType(this));
    return set->Resolve(std::move(arguments), this);
}

bool Type::IsCopyAssignable(Lexer::Access access) {
    auto set = AccessMember(analyzer.GetLvalueType(this), Lexer::TokenType::Assignment, access);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetLvalueType(this));
    return set->Resolve(std::move(arguments), this);
}

bool Type::IsMoveAssignable(Lexer::Access access) {
    auto set = AccessMember(analyzer.GetLvalueType(this), Lexer::TokenType::Assignment, access);
    std::vector<Type*> arguments;
    arguments.push_back(analyzer.GetLvalueType(this));
    arguments.push_back(analyzer.GetRvalueType(this));
    return set->Resolve(std::move(arguments), this);
}
bool Type::IsA(Type* self, Type* other, Lexer::Access access) {
    return
        other == self ||
        other == analyzer.GetRvalueType(self) ||
        self == analyzer.GetLvalueType(other) && other->IsCopyConstructible(access) ||
        self == analyzer.GetRvalueType(other) && other->IsMoveConstructible(access);
}


ConcreteExpression ConcreteExpression::BuildIncrement(bool postfix, Context c) {
    if (postfix) {
        auto copy = t->Decay()->BuildLvalueConstruction({ *this }, c);
        auto result = BuildIncrement(false, c);
        return ConcreteExpression(copy.t, c->gen->CreateChainExpression(copy.Expr, c->gen->CreateChainExpression(result.Expr, copy.Expr)));
    }
    return t->Decay()->BuildUnaryExpression(*this, Lexer::TokenType::Increment, c);
}

ConcreteExpression ConcreteExpression::BuildNegate(Context c) {
    return t->Decay()->BuildUnaryExpression(*this, Lexer::TokenType::Negate, c);
}

ConcreteExpression ConcreteExpression::BuildCall(ConcreteExpression l, ConcreteExpression r, Context c) {
    std::vector<ConcreteExpression> exprs;
    exprs.push_back(l);
    exprs.push_back(r);
    return BuildCall(std::move(exprs), c);
}

Util::optional<ConcreteExpression> ConcreteExpression::PointerAccessMember(std::string mem, Context c) {
    return BuildDereference(c).AccessMember(mem, c);
}

ConcreteExpression ConcreteExpression::BuildDereference(Context c) {
    return t->Decay()->BuildUnaryExpression(*this, Lexer::TokenType::Dereference, c);
}

ConcreteExpression ConcreteExpression::BuildCall(ConcreteExpression lhs, Context c) {
    std::vector<ConcreteExpression> exprs;
    exprs.push_back(lhs);
    return BuildCall(std::move(exprs), c);
}

ConcreteExpression ConcreteExpression::AddressOf(Context c) {
    // TODO: Remove this restriction, it is not very Wide.
    if (!IsLvalueType(t)) throw AddressOfNonLvalue(t, c.where, *c);
    return ConcreteExpression(c->GetPointerType(t->Decay()), Expr);
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
    if (t->IsComplexType(*c))
        assert(false && "Internal Compiler Error: Attempted to build a complex type into a register.");
    if (t->IsReference())
        return ConcreteExpression(t->Decay(), c->gen->CreateLoad(Expr));
    return *this;
}

ConcreteExpression ConcreteExpression::BuildCall(std::vector<ConcreteExpression> args, Context c) {
    return t->Decay()->BuildCall(*this, std::move(args), c);
}

ConcreteExpression ConcreteExpression::BuildCall(Context c) {
    return BuildCall(std::vector<ConcreteExpression>(), c);
}
Wide::Util::optional<ConcreteExpression> Type::AccessMember(ConcreteExpression e, std::string name, Context c) {
    if (IsReference())
        return Decay()->AccessMember(e, std::move(name), c);
    return Wide::Util::none;
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
    return t->Decay()->AccessMember(t, name, GetAccessSpecifier(c, t), *c);
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
    auto lhsaccess = GetAccessSpecifier(c, lhs.t);
    auto rhsaccess = GetAccessSpecifier(c, rhs.t);
    auto adlset = c->GetOverloadSet(lhs.AccessMember(type, c), c->GetOverloadSet(lhs.t->PerformADL(type, lhs.t, rhs.t, GetAccessSpecifier(c, lhs.t), *c), rhs.t->PerformADL(type, lhs.t, rhs.t, GetAccessSpecifier(c, rhs.t), *c)));
    std::vector<Type*> arguments;
    arguments.push_back(lhs.t);
    arguments.push_back(rhs.t);
    if (auto call = adlset->Resolve(arguments, *c, c.source)) {
        return call->Call({ lhs, rhs }, c);
    }
    
    // ADL has failed to find us a suitable operator, so fall back to defaults.
    // First implement binary op in terms of op=
    if (Assign.find(type) != Assign.end()) {
        auto lval = BuildLvalueConstruction({ lhs }, c).BuildBinaryExpression(rhs, Assign.at(type), c);
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
    adlset->IssueResolutionError(arguments, c);
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
    auto set = GetConstructorOverloadSet(*c, GetAccessSpecifier(c, this));
    auto call = set->Resolve(types, *c, c.source);
    if (!call) set->IssueResolutionError(types, c);
    return c->gen->CreateChainExpression(call->Call(std::move(args), c).Expr, mem);
}

ConcreteExpression Type::BuildValueConstruction(std::vector<ConcreteExpression> args, Context c) {
    if (IsComplexType(*c))
        assert(false && "Internal compiler error: Attempted to value construct a complex UDT.");
    if (args.size() == 1 && args[0].t == this)
        return args[0];
    auto mem = c->gen->CreateVariable(GetLLVMType(*c), alignment(*c));
    return ConcreteExpression(this, c->gen->CreateLoad(c->gen->CreateChainExpression(BuildInplaceConstruction(mem, std::move(args), c), mem)));
}
OverloadSet* Type::CreateADLOverloadSet(Lexer::TokenType what, Type* lhs, Type* rhs, Lexer::Access access, Analyzer& a) {
    if (IsReference())
        return Decay()->CreateADLOverloadSet(what, lhs, rhs, access, a);
    auto context = GetContext(a);
    if (!context)
        return a.GetOverloadSet();
    return GetContext(a)->AccessMember(GetContext(a), what, access, a);
}

OverloadSet* Type::PerformADL(Lexer::TokenType what, Type* lhs, Type* rhs, Lexer::Access access, Analyzer& a) {
    if (IsReference())
        return Decay()->PerformADL(what, lhs, rhs, access, a);
    if (ADLResults.find(lhs) != ADLResults.end()) {
        if (ADLResults[lhs].find(rhs) != ADLResults[lhs].end())
            if (ADLResults[lhs][rhs].find(access) != ADLResults[lhs][rhs].end())
                if (ADLResults[lhs][rhs][access].find(what) != ADLResults[lhs][rhs][access].end())
                    return ADLResults[lhs][rhs][access][what];
    }
    return ADLResults[lhs][rhs][access][what] = CreateADLOverloadSet(what, lhs, rhs, access, a);
}

OverloadSet* Type::CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Lexer::Access access, Analyzer& a) {
    if (IsReference())
        return Decay()->AccessMember(t, type, access, a);
    return a.GetOverloadSet();
}

OverloadSet* Type::AccessMember(Type* t, Lexer::TokenType type, Lexer::Access access, Analyzer& a) {
    if (this->OperatorOverloadSets.find(t) != OperatorOverloadSets.end())
        if (OperatorOverloadSets[t].find(access) != OperatorOverloadSets[t].end())
            if (OperatorOverloadSets[t][access].find(type) != OperatorOverloadSets[t][access].end())
                return OperatorOverloadSets[t][access][type];
    return OperatorOverloadSets[t][access][type] = CreateOperatorOverloadSet(t, type, access, a);
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
#pragma warning(disable : 4800)


Type* MetaType::GetConstantContext(Wide::Semantic::Analyzer& a) {
    return this;
}

OverloadSet* PrimitiveType::CreateOperatorOverloadSet(Type* t, Lexer::TokenType what, Lexer::Access access, Analyzer& a) {
    if (what != Lexer::TokenType::Assignment)
        return Type::CreateOperatorOverloadSet(t, what, access, a);

    if (t != a.GetLvalueType(this))
        return a.GetOverloadSet();
        
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    types.push_back(this);
    return a.GetOverloadSet(make_resolvable([](std::vector<ConcreteExpression> args, Context c) {
        return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, args[1].Expr));
    }, types, a));
}

OverloadSet* PrimitiveType::CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(a, Lexer::Access::Public);
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

OverloadSet* MetaType::CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) {
    if (access != Lexer::Access::Public) return GetConstructorOverloadSet(a, Lexer::Access::Public);
    std::vector<Type*> types;
    types.push_back(a.GetLvalueType(this));
    auto default_constructor = make_resolvable([](std::vector<ConcreteExpression> args, Context){ return args[0]; }, types, a);
    return a.GetOverloadSet(PrimitiveType::CreateConstructorOverloadSet(a, access), a.GetOverloadSet(default_constructor));
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
        if (!args[i].t->IsA(args[i].t, types[i], *c, GetAccessSpecifier(c, args[i].t)))
            Wide::Util::DebugBreak();
        out.push_back(types[i]->BuildValueConstruction({ args[i] }, c));
    }
    return out;
}

OverloadSet* Type::CreateDestructorOverloadSet(Analyzer& a) {
    struct DestructNothing : OverloadResolvable, Callable {
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final { 
            if (args.size() != 1) return Util::none;
            return args;
        }
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

    Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final {
        if (args.size() != types.size()) return Util::none;
        for (unsigned num = 0; num < types.size(); ++num) {
            auto t = args[num];
            if (!t->IsA(t, types[num], a, GetAccessSpecifier(source, t, a)))
                return Util::none;
        }
        return types;
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
OverloadSet* TupleInitializable::CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) {
    if (access != Lexer::Access::Public) return TupleInitializable::CreateConstructorOverloadSet(a, Lexer::Access::Public);
    struct TupleConstructor : public OverloadResolvable, Callable {
        TupleConstructor(TupleInitializable* p) : self(p) {}
        TupleInitializable* self;
        Callable* GetCallableForResolution(std::vector<Type*>, Analyzer& a) { return this; }
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> args, Analyzer& a, Type* source) override final {
            if (args.size() != 2) return Util::none;
            if (args[0] != a.GetLvalueType(self->GetSelfAsType())) return Util::none;
            auto tup = dynamic_cast<TupleType*>(args[1]->Decay());
            if (!tup) return Util::none;
            if (!tup->IsA(args[1], self->GetSelfAsType(), a, GetAccessSpecifier(source, tup, a))) return Util::none;
            return args;
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
                auto expr = members[i]->BuildInplaceConstruction(memory.Expr, { argument }, c);
                p = p ? c->gen->CreateChainExpression(p, expr) : expr;
            }
            return ConcreteExpression(c->GetLvalueType(self->GetSelfAsType()), c->gen->CreateChainExpression(p, args[0].Expr));
        }
    };
    return a.GetOverloadSet(a.arena.Allocate<TupleConstructor>(this));
}
OverloadSet* Type::GetConstructorOverloadSet(Analyzer& a, Lexer::Access access) {
    if (ConstructorOverloadSet.find(access) != ConstructorOverloadSet.end())
        return ConstructorOverloadSet[access];
    return ConstructorOverloadSet[access] = CreateConstructorOverloadSet(a, access);
}
OverloadSet* Type::GetDestructorOverloadSet(Analyzer& a) {
    if (!DestructorOverloadSet)
        DestructorOverloadSet = CreateDestructorOverloadSet(a);
    return DestructorOverloadSet;
}
ConcreteExpression Type::BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, std::vector<ConcreteExpression> destructors, Context c) {
    for (auto x : destructors)
        c(x);
    return val.BuildCall(std::move(args), c);
}

ConcreteExpression Type::BuildUnaryExpression(ConcreteExpression self, Lexer::TokenType type, Context c) {
    // Increment funkyness already taken care of
    auto opset = AccessMember(self.t, type, GetAccessSpecifier(c, self.t), *c);
    auto callable = opset->Resolve({ self.t }, *c, c.source);
    if (!callable) {
        if (type != Lexer::TokenType::Negate)
            opset->IssueResolutionError({ self.t }, c);
        return ConcreteExpression(c->GetBooleanType(), c->gen->CreateNegateExpression(self.BuildBooleanConversion(c)));
    }
    return callable->Call({ self }, c);
}
ConcreteExpression Type::BuildCall(ConcreteExpression val, std::vector<ConcreteExpression> args, Context c) {
    auto set = val.AccessMember(Lexer::TokenType::OpenBracket, c);
    args.insert(args.begin(), val);
    std::vector<Type*> types;
    for (auto arg : args)
        types.push_back(arg.t);
    auto call = set->Resolve(types, *c, c.source);
    if (!call) set->IssueResolutionError(types, c);
    return call->Call(args, c);
}

Codegen::Expression* Type::BuildBooleanConversion(ConcreteExpression val, Context c) {
    if (IsReference())
        return Decay()->BuildBooleanConversion(val, c);
    throw NoBooleanConversion(val.t, c.where, *c);
}            

Codegen::Expression* BaseType::CreateVTable(std::vector<std::pair<BaseType*, unsigned>> path, Analyzer& a) {
    auto vptrty = GetVirtualPointerType(a);
    auto base_vfunc_ty = [=](llvm::Module* mod) -> llvm::Type* {
        // The vtable pointer should be pointer to function pointer.
        // We use i32()**, Clang uses i32(...)**, either way.
        auto ptrty = llvm::dyn_cast<llvm::PointerType>(vptrty(mod));
        return ptrty->getElementType();
    };
    std::vector<Codegen::Expression*> elements;
    unsigned offset_total = 0;
    for (auto base : path)
        offset_total += base.second;
    for (auto func : GetVtableLayout(a)) {
        bool found = false;
        auto local_copy = offset_total;
        for (auto more_derived : path) {
            if (auto expr = more_derived.first->FunctionPointerFor(func.name, func.args, func.ret, local_copy, a)) {
                elements.push_back(a.gen->CreatePointerCast(expr, base_vfunc_ty));
                found = true;
                break;
            }
            local_copy -= more_derived.second;
        }
        if (func.abstract && !found) {
            // It's possible we didn't find a more derived impl.
            // Just use a null pointer instead of the Itanium ABI function for now.
            elements.push_back(a.gen->CreateNull(base_vfunc_ty));
        }
        // Should have found base class impl!
        if (!found)
            throw std::runtime_error("le fuck");
    }
    return a.gen->CreateConstantArray(base_vfunc_ty, elements);
}      

Codegen::Expression* BaseType::GetVTablePointer(std::vector<std::pair<BaseType*, unsigned>> path, Analyzer& a) {
    if (ComputedVTables.find(path) != ComputedVTables.end())
        return ComputedVTables.at(path);
    return ComputedVTables[path] = CreateVTable(path, a);
}

Codegen::Expression* BaseType::SetVirtualPointers(Codegen::Expression* self, Analyzer& a) {
    return SetVirtualPointers({}, self, a);
}

Codegen::Expression* BaseType::SetVirtualPointers(std::vector<std::pair<BaseType*, unsigned>> path, Codegen::Expression* self, Analyzer& a) {
    // Set the base vptrs first, because some Clang types share vtables with their base and this would give the shared base the wrong vtable.
    auto nop = (Codegen::Expression*)a.gen->CreateNop();
    for (auto base : GetBases(a)) {
        path.push_back(std::make_pair(this, base.second));
        auto more = base.first->SetVirtualPointers(path, AccessBase(base.first->GetSelfAsType(), self, a), a);
        nop = a.gen->CreateChainExpression(nop, more);
        path.pop_back();
    }
    // If we actually have a vptr, then set it; else just set the bases.
    auto vptr = GetVirtualPointer(self, a);
    if (!vptr)
        return nop;
    path.push_back(std::make_pair(this, 0));
    return a.gen->CreateChainExpression(nop, a.gen->CreateStore(vptr, GetVTablePointer(path, a)));
}