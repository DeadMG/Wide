#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Expression.h>

using namespace Wide;
using namespace Semantic;

TupleType::TupleType(std::vector<Type*> types, Analyzer& a)
: contents(std::move(types)), AggregateType(a) {}

std::shared_ptr<Expression> TupleType::ConstructFromLiteral(std::vector<std::shared_ptr<Expression>> exprs, Context c) {
    assert(exprs.size() == GetMembers().size());
    for (std::size_t i = 0; i < exprs.size(); ++i)
        assert(GetMembers()[i] == exprs[i]->GetType()->Decay());

    // If we have no members, return value (will be undef).
    if (GetMembers().size() == 0)
        return BuildValueConstruction({}, c);

    // If we are complex, then build directly in-place.
    // Else try to avoid making an allocation.
    std::vector<std::shared_ptr<Expression>> initializers;
    auto self = std::make_shared<ImplicitTemporaryExpr>(this, c);
    if (IsComplexType()) {
        for (std::size_t i = 0; i < exprs.size(); ++i) {
            initializers.push_back(GetMembers()[i]->BuildInplaceConstruction(PrimitiveAccessMember(self, i), { std::move(exprs[i]) }, c));
        }
    } else {
        for (std::size_t i = 0; i < exprs.size(); ++i) {
            initializers.push_back(GetMembers()[i]->BuildValueConstruction({ std::move(exprs[i]) }, c));
            assert(initializers.back()->GetType() == GetMembers()[i]);
        }
    }

    struct TupleConstruction : Expression {
        TupleConstruction(std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> inits, TupleType* tup, Context c)
            : self(std::move(self)), inits(std::move(inits)), tuplety(tup)
        {
            if (!tuplety->IsTriviallyDestructible())
                destructor = tuplety->BuildDestructorCall(self, c, true);
        }
        TupleType* tuplety;
        std::vector<std::shared_ptr<Expression>> inits;
        std::shared_ptr<Expression> self; 
        std::function<void(CodegenContext&)> destructor;
        Type* GetType() override final {
            return self->GetType()->Decay();
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            if (GetType()->IsComplexType()) {
                for (auto&& init : inits)
                    init->GetValue(con);
                if (destructor)
                    con.AddDestructor(destructor);
                return self->GetValue(con);
            }
            llvm::Value* agg = llvm::UndefValue::get(tuplety->GetLLVMType(con));
            // The inits will be values, so insert them.
            for (unsigned i = 0; i < tuplety->GetMembers().size(); ++i) {
                auto ty = inits[i]->GetType();
                auto memty = tuplety->GetMembers()[i];
                assert(ty == memty);
                auto val = inits[i]->GetValue(con);
                agg = con->CreateInsertValue(agg, val, { boost::get<LLVMFieldIndex>(tuplety->GetLocation(i)).index });
            }
            return agg;
        }
    };

    return Wide::Memory::MakeUnique<TupleConstruction>(self, std::move(initializers), this, c);
}

bool TupleType::IsSourceATarget(Type* source, Type* target, Type* context) {
    // Only value conversions allowed
    if (IsLvalueType(target)) return false;

    // We have nothing to say about sources that are not tuples.
    auto sourcetup = dynamic_cast<TupleType*>(source->Decay());
    if (!sourcetup) return false;

    struct source_expr : public Expression {
        source_expr(Type* s) :source(s) {}
        Type* source;
        Type* GetType() { return source; }
        llvm::Value* ComputeValue(CodegenContext& e) { assert(false); return nullptr; }
    };
    auto expr = std::make_shared<source_expr>(source);
    std::vector<Type*> targettypes;

    // The target should be either tuple initializable or a tuple. We is-a them if the types are is-a.
    if (auto tupty = dynamic_cast<TupleType*>(target->Decay()))
        targettypes = tupty->GetMembers();
    else if (auto tupleinit = dynamic_cast<TupleInitializable*>(target->Decay()))
        if (auto types = tupleinit->GetTypesForTuple())
            targettypes = *types;
        else
            return false;
    else
        return false;
    if (GetMembers().size() != targettypes.size()) return false;
    for (std::size_t i = 0; i < GetMembers().size(); ++i)
        if (!Type::IsFirstASecond(sourcetup->PrimitiveAccessMember(expr, i)->GetType(), targettypes[i], context)) return false;
    return true;
}
std::string TupleType::explain() {
    std::string name = "{ ";
    for (auto& ty : GetMembers()) {
        if (&ty != &GetMembers().back()) {
            name += ty->explain() + ", ";
        } else
            name += ty->explain();
    }
    name += " }";
    return name;
}