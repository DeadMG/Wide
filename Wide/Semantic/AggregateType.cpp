#include <Wide/Semantic/AggregateType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/OverloadSet.h>
#include <sstream>
#include <Wide/Codegen/Generator.h>

#pragma warning(push, 0)
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>
#pragma warning(pop)

#include <Wide/Codegen/GeneratorMacros.h>

using namespace Wide;
using namespace Semantic;

AggregateType::Layout::Layout(const std::vector<Type*>& types, Wide::Semantic::Analyzer& a)
: allocsize(0)
, align(1)
, IsComplex(false)
, copyassignable(true)
, copyconstructible(true)
, moveassignable(true)
, moveconstructible(true)
, constant(true)
{
    // Treat empties differently to match Clang's expectations

    if (types.empty()) {
        llvmtypes.push_back(a.GetIntegralType(8, true)->GetLLVMType(a));
        return;
    }

    auto adjust_alignment = [this](std::size_t alignment) {
        if (allocsize % alignment != 0) {
            auto adjustment = alignment - (allocsize % alignment);
            allocsize += adjustment;
            llvmtypes.push_back([adjustment](llvm::Module* mod) {
                return llvm::ArrayType::get(llvm::IntegerType::getInt8Ty(mod->getContext()), adjustment);
            });
        }
        align = std::max(alignment, align);
        assert(allocsize % alignment == 0);
    };

    for (auto ty : types) {
        // Check that we are suitably aligned for the next member and if not, align it with some padding.
        auto talign = ty->alignment(a);
        adjust_alignment(talign);

        // Add the type itself to the list- zero-based index.
        Offsets.push_back(allocsize);
        allocsize += ty->size(a);
        FieldIndices.push_back(llvmtypes.size());
        llvmtypes.push_back(ty->GetLLVMType(a));

        IsComplex = IsComplex || ty->IsComplexType(a);
        copyconstructible = copyconstructible && ty->IsCopyConstructible(a, Lexer::Access::Public);
        moveconstructible = moveconstructible && ty->IsMoveConstructible(a, Lexer::Access::Public);
        copyassignable = copyassignable && ty->IsCopyAssignable(a, Lexer::Access::Public);
        moveassignable = moveassignable && ty->IsMoveAssignable(a, Lexer::Access::Public);
        constant = constant && ty->GetConstantContext(a);
    }

    // Fix the alignment of the whole structure
    adjust_alignment(align);    
}
std::size_t AggregateType::size(Analyzer& a) {
    return GetLayout(a).allocsize;
}
std::size_t AggregateType::alignment(Analyzer& a) {
    return GetLayout(a).align;
}
bool AggregateType::IsComplexType(Analyzer& a) {
    return GetLayout(a).IsComplex;
}

bool AggregateType::IsMoveAssignable(Analyzer& a, Lexer::Access access) {
    return GetLayout(a).moveassignable;
}
bool AggregateType::IsMoveConstructible(Analyzer& a, Lexer::Access access) {
    return GetLayout(a).moveconstructible;
}
bool AggregateType::IsCopyAssignable(Analyzer& a, Lexer::Access access) {
    return GetLayout(a).copyassignable;
}
bool AggregateType::IsCopyConstructible(Analyzer& a, Lexer::Access access) {
    return GetLayout(a).copyconstructible;
}

std::function<llvm::Type*(llvm::Module*)> AggregateType::GetLLVMType(Analyzer& a) {
    std::stringstream stream;
    stream << "struct.__" << this;
    auto llvmname = stream.str();
    return [this, &a, llvmname](llvm::Module* m) -> llvm::Type* {
        if (m->getTypeByName(llvmname)) {
            if (GetContents().empty())
                a.gen->AddEliminateType(m->getTypeByName(llvmname));
            return m->getTypeByName(llvmname);
        }
        std::vector<llvm::Type*> types;
        for (auto&& x : GetLayout(a).llvmtypes)
            types.push_back(x(m));
        auto ty = llvm::StructType::create(types, llvmname);
        if (GetContents().empty())
            a.gen->AddEliminateType(ty);
        return ty;
    };
}

OverloadSet* AggregateType::CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Lexer::Access access, Analyzer& a) {
    if (type != Lexer::TokenType::Assignment)
        return a.GetOverloadSet();
    if (access != Lexer::Access::Public)
        return AccessMember(t, type, Lexer::Access::Public, a);

    // Similar principle to constructor
    std::unordered_set<OverloadResolvable*> set;
    std::function<Type*(Type*)> modify;
    auto createoperator = [this, &modify, &a, &set] {
        std::vector<Type*> types;
        types.push_back(a.GetLvalueType(this));
        types.push_back(modify(this));
        set.insert(make_resolvable([this, modify](std::vector<ConcreteExpression> args, Context c) {
            if (GetContents().size() == 0)
                return args[0];

            if (!IsComplexType(*c))
                return ConcreteExpression(args[0].t, c->gen->CreateStore(args[0].Expr, args[1].BuildValue(c).Expr));

            Codegen::Expression* move = nullptr;
            // For every type, call the operator
            for (std::size_t i = 0; i < GetContents().size(); ++i) {
                auto type = GetContents()[i];
                ConcreteExpression lhs = PrimitiveAccessMember(args[0], i, *c);
                std::vector<Type*> types;
                types.push_back(c->GetLvalueType(type));
                types.push_back(modify(type));
                auto expr = type->AccessMember(modify(type), Lexer::TokenType::Assignment, Lexer::Access::Public, *c)->Resolve(types, *c, this)->Call({ lhs, PrimitiveAccessMember(args[1], i, *c) }, c).Expr;
                move = move ? c->gen->CreateChainExpression(move, expr) : expr;
            }

            return ConcreteExpression(args[0].t, c->gen->CreateChainExpression(move, args[0].Expr));
        }, types, a));
    };

    if (GetLayout(a).moveassignable) {
        modify = [&a](Type* t) { return a.GetRvalueType(t); };
        createoperator();
    }
    if (GetLayout(a).copyassignable) {
        modify = [&a](Type* t) { return a.GetLvalueType(t); };
        createoperator();
    }
    return a.GetOverloadSet(set);
}

Type* AggregateType::GetConstantContext(Analyzer& a) {
   for(auto ty : GetContents())
       if (!ty->GetConstantContext(a))
           return nullptr;
   return this;
}

ConcreteExpression AggregateType::PrimitiveAccessMember(ConcreteExpression e, unsigned num, Analyzer& a) {
    auto FieldIndex = GetLayout(a).FieldIndices[num];
    auto obj = a.gen->CreateFieldExpression(e.Expr, FieldIndex);
    // If it's not a reference no collapse or anything else necessary, just produce the value.
    if (!e.t->IsReference())
        return ConcreteExpression(GetContents()[num], obj);

    // If they're both references, then collapse.
    if (e.t->IsReference() && GetContents()[num]->IsReference())
        return ConcreteExpression(GetContents()[num], a.gen->CreateLoad(obj));

    // Need to preserve the lvalue/rvalueness of the source.
    if (e.t == a.GetLvalueType(e.t->Decay()))
        return ConcreteExpression(a.GetLvalueType(GetContents()[num]), obj);

    // If not an lvalue or a value, must be rvalue.
    return ConcreteExpression(a.GetRvalueType(GetContents()[num]), obj);
}

OverloadSet* AggregateType::CreateNondefaultConstructorOverloadSet(Analyzer& a) {
    std::unordered_set<OverloadResolvable*> set;

    // First, move/copy
    std::function<Type*(Type*)> modify;
    auto createconstructor = [this, &modify, &a, &set] {
        std::vector<Type*> types;
        types.push_back(a.GetLvalueType(this));
        types.push_back(modify(this));
        set.insert(make_resolvable([this, modify](std::vector<ConcreteExpression> args, Context c) {
            if (GetContents().size() == 0)
                return args[0];

            Codegen::Expression* move = nullptr;
            // For every type, call the move constructor.
            for (std::size_t i = 0; i < GetContents().size(); ++i) {
                auto ty = GetContents()[i];
                ConcreteExpression memory(c->GetLvalueType(ty), c->gen->CreateFieldExpression(args[0].Expr, GetLayout(*c).FieldIndices[i]));
                std::vector<Type*> types;
                types.push_back(c->GetLvalueType(ty));
                types.push_back(modify(ty));
                auto set = ty->GetConstructorOverloadSet(*c, Lexer::Access::Public);
                auto callable = set->Resolve(types, *c, this);
                auto expr = callable->Call({ memory, PrimitiveAccessMember(args[1], i, *c) }, c).Expr;
                move = move ? c->gen->CreateChainExpression(move, expr) : expr;
            }

            return ConcreteExpression(args[0].t, move);
        }, types, a));
    };

    if (GetLayout(a).moveconstructible) {
        modify = [&a](Type* t) { return a.GetRvalueType(t); };
        createconstructor();
    }
    if (GetLayout(a).copyconstructible) {
        modify = [&a](Type* t) { return a.GetLvalueType(t); };
        createconstructor();
    }
    return a.GetOverloadSet(set);
}
OverloadSet* AggregateType::CreateConstructorOverloadSet(Analyzer& a, Lexer::Access access) {
    // Use create instead of get because get will return the more derived class's constructors and that is not what we want.
    if (access != Lexer::Access::Public) return AggregateType::CreateConstructorOverloadSet(a, Lexer::Access::Public);
    std::unordered_set<OverloadResolvable*> set;
    // Then default.
    auto is_default_constructible = [this, &a] {
        for (auto ty : GetContents()) {
            std::vector<Type*> types;
            types.push_back(a.GetLvalueType(ty));
            if (!ty->GetConstructorOverloadSet(a, Lexer::Access::Public)->Resolve(types, a, this))
                return false;
        }
        return true;
    };
    if (is_default_constructible()) {
        std::vector<Type*> types;
        types.push_back(a.GetLvalueType(this));
        set.insert(make_resolvable([this](std::vector<ConcreteExpression> args, Context c) {
            if (GetContents().size() == 0)
                return args[0];

            Codegen::Expression* def = nullptr;
           
            for (std::size_t i = 0; i < GetContents().size(); ++i) {
                auto ty = GetContents()[i];
                ConcreteExpression memory(c->GetLvalueType(ty), c->gen->CreateFieldExpression(args[0].Expr, GetLayout(*c).FieldIndices[i]));
                std::vector<Type*> types;
                types.push_back(c->GetLvalueType(ty));
                auto expr = ty->GetConstructorOverloadSet(*c, Lexer::Access::Public)->Resolve(types, *c, this)->Call({ memory }, c).Expr;
                def = def ? c->gen->CreateChainExpression(def, expr) : expr;
            }
            return ConcreteExpression(c->GetLvalueType(this), def);
        }, types, a));
    }
    return a.GetOverloadSet(a.GetOverloadSet(set), AggregateType::CreateNondefaultConstructorOverloadSet(a));
}
OverloadSet* AggregateType::CreateDestructorOverloadSet(Analyzer& a) {
    struct Destructor : OverloadResolvable, Callable {
        AggregateType* self;
        Destructor(AggregateType* ty) : self(ty) {}
        Util::optional<std::vector<Type*>> MatchParameter(std::vector<Type*> types, Analyzer& a, Type* source) override final { 
            if (types.size() == 1) return types;
            return Util::none;
        }
        Callable* GetCallableForResolution(std::vector<Type*> tys, Analyzer& a) override final { return this; }
        std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
        ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
            if (self->GetContents().size() == 0)
                return args[0];
            Codegen::Expression* expr = nullptr;
            for (unsigned i = 0; i < self->GetContents().size(); ++i) {
                auto member = ConcreteExpression(c->GetLvalueType(self->GetContents()[i]), c->gen->CreateFieldExpression(args[0].Expr, self->GetFieldIndex(*c, i)));
                std::vector<Type*> types;
                types.push_back(member.t);
                auto des = self->GetContents()[i]->GetDestructorOverloadSet(*c)->Resolve(types, *c, self)->Call({ member }, c).Expr;
                expr = expr ? c->gen->CreateChainExpression(expr, des) : des;
            }
            return ConcreteExpression(c->GetLvalueType(self), expr);
        }
    };
    return a.GetOverloadSet(a.arena.Allocate<Destructor>(this));
}