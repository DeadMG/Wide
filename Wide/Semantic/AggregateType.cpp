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

AggregateType::AggregateType(std::vector<Type*> types, Wide::Semantic::Analyzer& a)
: contents(std::move(types))
, allocsize(0)
, align(1)
, IsComplex(false)
, copyassignable(true)
, copyconstructible(true)
, moveassignable(true)
, moveconstructible(true)
, constant(true)
{
    // Treat empties differently to match Clang's expectations

    if (contents.empty()) {
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

    for (auto ty : contents) {
        // Check that we are suitably aligned for the next member and if not, align it with some padding.
        auto talign = ty->alignment(a);
        adjust_alignment(talign);

        // Add the type itself to the list- zero-based index.
        allocsize += ty->size(a);
        FieldIndices.push_back(llvmtypes.size());
        llvmtypes.push_back(ty->GetLLVMType(a));

        IsComplex = IsComplex || ty->IsComplexType(a);
        copyconstructible = copyconstructible && ty->IsCopyConstructible(a);
        moveconstructible = moveconstructible && ty->IsMoveConstructible(a);
        copyassignable = copyassignable && ty->IsCopyAssignable(a);
        moveassignable = moveassignable && ty->IsMoveAssignable(a);
        constant = constant && ty->GetConstantContext(a);
    }

    // Fix the alignment of the whole structure
    adjust_alignment(align);    
}
std::size_t AggregateType::size(Analyzer& a) {
    return allocsize;
}
std::size_t AggregateType::alignment(Analyzer& a) {
    return align;
}
bool AggregateType::IsComplexType(Analyzer& a) {
    return IsComplex;
}

bool AggregateType::IsMoveAssignable(Analyzer& a) {
    return moveassignable;
}
bool AggregateType::IsMoveConstructible(Analyzer& a) {
    return moveconstructible;
}
bool AggregateType::IsCopyAssignable(Analyzer& a) {
    return copyassignable;
}
bool AggregateType::IsCopyConstructible(Analyzer& a) {
    return copyconstructible;
}

std::function<llvm::Type*(llvm::Module*)> AggregateType::GetLLVMType(Analyzer& a) {
    std::stringstream stream;
    stream << "struct.__" << this;
    auto llvmname = stream.str();
    return [this, &a, llvmname](llvm::Module* m) -> llvm::Type* {
        if (m->getTypeByName(llvmname)) {
            if (contents.empty())
                a.gen->AddEliminateType(m->getTypeByName(llvmname));
            return m->getTypeByName(llvmname);
        }
        std::vector<llvm::Type*> types;
        for (auto&& x : llvmtypes)
            types.push_back(x(m));
        auto ty = llvm::StructType::create(types, llvmname);
        if (contents.empty())
            a.gen->AddEliminateType(ty);
        return ty;
    };
}

OverloadSet* AggregateType::CreateOperatorOverloadSet(Type* t, Lexer::TokenType type, Analyzer& a) {
    if (type != Lexer::TokenType::Assignment)
        return a.GetOverloadSet();

    // Similar principle to constructor
    std::unordered_set<OverloadResolvable*> set;
    std::function<Type*(Type*)> modify;
    auto createoperator = [this, &modify, &a, &set] {
        std::vector<Type*> types;
        types.push_back(a.GetLvalueType(this));
        types.push_back(modify(this));
        set.insert(make_resolvable([this, modify](std::vector<ConcreteExpression> args, Context c) {
            if (contents.size() == 0)
                return args[0];

            Codegen::Expression* move = nullptr;
            // For every type, call the move constructor.
            for (std::size_t i = 0; i < contents.size(); ++i) {
                ConcreteExpression lhs = PrimitiveAccessMember(args[1], i, *c);
                std::vector<Type*> types;
                types.push_back(c->GetLvalueType(contents[i]));
                types.push_back(modify(contents[i]));
                auto expr = contents[i]->AccessMember(modify(contents[i]), Lexer::TokenType::Assignment, *c)->Resolve(types, *c)->Call(lhs, PrimitiveAccessMember(args[1], i, *c), c).Expr;
                move = move ? c->gen->CreateChainExpression(move, expr) : expr;
            }

            return ConcreteExpression(args[0].t, c->gen->CreateChainExpression(move, args[0].Expr));
        }, types, a));
    };

    if (moveassignable) {
        modify = [&a](Type* t) { return a.GetRvalueType(t); };
        createoperator();
    }
    if (copyassignable) {
        modify = [&a](Type* t) { return a.GetLvalueType(t); };
        createoperator();
    }
    return a.GetOverloadSet(set);
}

std::vector<Type*> AggregateType::GetMembers() {
    return contents;
}

Type* AggregateType::GetConstantContext(Analyzer& a) {
   for(auto ty : contents)
       if (!ty->GetConstantContext(a))
           return nullptr;
   return this;
}

ConcreteExpression AggregateType::PrimitiveAccessMember(ConcreteExpression e, unsigned num, Analyzer& a) {
    auto FieldIndex = FieldIndices[num];
    auto obj = a.gen->CreateFieldExpression(e.Expr, FieldIndex);
    // If it's not a reference no collapse or anything else necessary, just produce the value.
    if (!e.t->IsReference())
        return ConcreteExpression(contents[num], obj);

    // If they're both references, then collapse.
    if (e.t->IsReference() && contents[num]->IsReference())
        return ConcreteExpression(contents[num], a.gen->CreateLoad(obj));

    // Need to preserve the lvalue/rvalueness of the source.
    if (e.t == a.GetLvalueType(e.t->Decay()))
        return ConcreteExpression(a.GetLvalueType(contents[num]), obj);

    // If not an lvalue or a value, must be rvalue.
    return ConcreteExpression(a.GetRvalueType(contents[num]), obj);
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
            if (contents.size() == 0)
                return args[0];

            Codegen::Expression* move = nullptr;
            // For every type, call the move constructor.
            for (std::size_t i = 0; i < contents.size(); ++i) {
                ConcreteExpression memory(c->GetLvalueType(contents[i]), c->gen->CreateFieldExpression(args[0].Expr, FieldIndices[i]));
                std::vector<Type*> types;
                types.push_back(c->GetLvalueType(contents[i]));
                types.push_back(modify(contents[i]));
                auto expr = contents[i]->GetConstructorOverloadSet(*c)->Resolve(types, *c)->Call(memory, PrimitiveAccessMember(args[1], i, *c), c).Expr;
                move = move ? c->gen->CreateChainExpression(move, expr) : expr;
            }

            return ConcreteExpression(args[0].t, move);
        }, types, a));
    };

    if (moveconstructible) {
        modify = [&a](Type* t) { return a.GetRvalueType(t); };
        createconstructor();
    }
    if (copyconstructible) {
        modify = [&a](Type* t) { return a.GetLvalueType(t); };
        createconstructor();
    }
    return a.GetOverloadSet(set);
}
OverloadSet* AggregateType::CreateConstructorOverloadSet(Analyzer& a) {
    std::unordered_set<OverloadResolvable*> set;
    // Then default.
    auto is_default_constructible = [this, &a] {
        for (auto ty : contents) {
            std::vector<Type*> types;
            types.push_back(a.GetLvalueType(ty));
            if (!ty->GetConstructorOverloadSet(a)->Resolve(types, a))
                return false;
        }
        return true;
    };
    if (is_default_constructible()) {
        std::vector<Type*> types;
        types.push_back(a.GetLvalueType(this));
        set.insert(make_resolvable([this](std::vector<ConcreteExpression> args, Context c) {
            if (contents.size() == 0)
                return args[0];

            Codegen::Expression* def = nullptr;
            for (std::size_t i = 0; i < contents.size(); ++i) {
                ConcreteExpression memory(c->GetLvalueType(contents[i]), c->gen->CreateFieldExpression(args[0].Expr, FieldIndices[i]));
                std::vector<Type*> types;
                types.push_back(c->GetLvalueType(contents[i]));
                auto expr = contents[i]->GetConstructorOverloadSet(*c)->Resolve(types, *c)->Call(memory, c).Expr;
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

        unsigned GetArgumentCount() override final { return 1; }
        Type* MatchParameter(Type* t, unsigned num, Analyzer& a) override final { assert(num == 0); return t; }
        Callable* GetCallableForResolution(std::vector<Type*> tys, Analyzer& a) override final { return this; }
        std::vector<ConcreteExpression> AdjustArguments(std::vector<ConcreteExpression> args, Context c) override final { return args; }
        ConcreteExpression CallFunction(std::vector<ConcreteExpression> args, Context c) override final {
            if (self->contents.size() == 0)
                return args[0];
            Codegen::Expression* expr = nullptr;
            for (unsigned i = 0; i < self->contents.size(); ++i) {
                auto member = ConcreteExpression(c->GetLvalueType(self->contents[i]), c->gen->CreateFieldExpression(args[0].Expr, self->GetFieldIndex(i)));
                std::vector<Type*> types;
                types.push_back(member.t);
                auto des = self->contents[i]->GetDestructorOverloadSet(*c)->Resolve(types, *c)->Call(member, c).Expr;
                expr = expr ? c->gen->CreateChainExpression(expr, des) : des;
            }
            return ConcreteExpression(c->GetLvalueType(self), expr);
        }
    };
    return a.GetOverloadSet(a.arena.Allocate<Destructor>(this));
}