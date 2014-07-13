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

    auto self = std::make_shared<ImplicitTemporaryExpr>(this, c);
    if (GetMembers().size() == 0)
        return std::move(self);
    std::vector<std::shared_ptr<Expression>> initializers;
    for (std::size_t i = 0; i < exprs.size(); ++i) {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(GetMembers()[i]));
        types.push_back(exprs[i]->GetType());
        auto conset = GetMembers()[i]->GetConstructorOverloadSet(GetAccessSpecifier(c.from, GetMembers()[i]));
        auto call = conset->Resolve(types, c.from);
        if (!call) conset->IssueResolutionError(types, c);
        initializers.push_back(call->Call({ PrimitiveAccessMember(self, i), std::move(exprs[i]) }, c));
    }

    struct TupleConstruction : Expression {
        TupleConstruction(std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> inits, std::function<void(CodegenContext&)> destructor)
        : self(std::move(self)), inits(std::move(inits)), destructor(destructor) {}
        std::vector<std::shared_ptr<Expression>> inits;
        std::shared_ptr<Expression> self; 
        std::function<void(CodegenContext&)> destructor;
        Type* GetType() override final {
            return self->GetType()->analyzer.GetRvalueType(self->GetType()->Decay());
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            for (auto&& init : inits)
                init->GetValue(con);
            if (self->GetType()->IsComplexType())
                con.AddDestructor(destructor);
            return self->GetValue(con);
        }
    };

    return Wide::Memory::MakeUnique<TupleConstruction>(self, std::move(initializers), BuildDestructorCall(self, c, true));
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
        llvm::Value* ComputeValue(CodegenContext& e) { return nullptr; }
    };
    auto expr = std::make_shared<source_expr>(source);
    std::vector<Type*> targettypes;

    // The target should be either tuple initializable or a tuple but we have a different test.
    if (auto tupleinit = dynamic_cast<TupleInitializable*>(target->Decay())) {
        if (auto types = tupleinit->GetTypesForTuple())
            targettypes = *types;
        else
            return false;
        if (GetMembers().size() != targettypes.size()) return false;
        for (std::size_t i = 0; i < GetMembers().size(); ++i) {
            std::vector<Type*> types;
            types.push_back(analyzer.GetLvalueType(targettypes.at(i)));
            types.push_back(sourcetup->PrimitiveAccessMember(expr, i)->GetType());
            if (!targettypes.at(i)->GetConstructorOverloadSet(GetAccessSpecifier(context, targettypes.at(i)))->Resolve(types, this))
                return false;
        }
        return true;
    }
    if (auto tupty = dynamic_cast<TupleType*>(target->Decay()))
        targettypes = tupty->GetMembers();
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