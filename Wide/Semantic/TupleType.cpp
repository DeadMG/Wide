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
    //assert(exprs.size() == GetMembers().size());
    //for (std::size_t i = 0; i < exprs.size(); ++i)
    //    assert(GetMembers()[i] == exprs[i]->GetType()->Decay());

    // If we have no members, return value (will be undef).
    if (GetMembers().size() == 0)
        return BuildValueConstruction({}, c);

    // If we are complex, then build directly in-place.
    // Else try to avoid making an allocation.
    std::vector<std::shared_ptr<Expression>> initializers;
    auto self = CreateTemporary(this, c);
    for (std::size_t i = 0; i < exprs.size(); ++i) {
        initializers.push_back(Type::BuildInplaceConstruction(PrimitiveAccessMember(self, i), { std::move(exprs[i]) }, c));
    }

    auto destructor = BuildDestructorCall(Expression::NoInstance(), self, c, true);
    return CreatePrimGlobal(Range::Container(initializers), this, [=](CodegenContext& con) -> llvm::Value* {
        for (auto&& init : initializers)
            init->GetValue(con);
        if (destructor)
            con.AddDestructor(destructor);
        if (AlwaysKeepInMemory(con))
            return self->GetValue(con);
        return con->CreateLoad(self->GetValue(con));
    });
}

bool TupleType::IsSourceATarget(Type* source, Type* target, Type* context) {
    // Only value conversions allowed
    if (IsLvalueType(target)) return false;

    // We have nothing to say about sources that are not tuples.
    auto sourcetup = dynamic_cast<TupleType*>(source->Decay());
    if (!sourcetup) return false;

    std::vector<Type*> targettypes;
    std::vector<Type*> sourcetypes;
    // The target should be either tuple initializable or a tuple. We is-a them if the types are is-a.
    auto get_types = [](Type* type) -> Wide::Util::optional<std::vector<Type*>> {
        if (auto tupty = dynamic_cast<TupleType*>(type->Decay()))
            return tupty->GetMembers();
        else if (auto tupleinit = dynamic_cast<TupleInitializable*>(type->Decay()))
            if (auto types = tupleinit->GetTypesForTuple())
                return *types;
        return Wide::Util::none;
    };
    if (auto sourcetypes = get_types(source)) {
        if (auto targettypes = get_types(target)) {
            if (sourcetypes->size() != targettypes->size()) return false;
            for (std::size_t i = 0; i < sourcetypes->size(); ++i)
                if (!Type::IsFirstASecond(Semantic::CollapseType(source, sourcetypes->at(i)), Semantic::CollapseType(target, targettypes->at(i)), context)) 
                    return false;
            return true;
        }
    }
    return false;
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