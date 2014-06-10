#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Expression.h>

using namespace Wide;
using namespace Semantic;

TupleType::TupleType(std::vector<Type*> types, Analyzer& a)
: contents(std::move(types)), AggregateType(a) {}

std::unique_ptr<Expression> TupleType::ConstructFromLiteral(std::vector<std::unique_ptr<Expression>> exprs, Context c) {
    assert(exprs.size() == GetMembers().size());
    for (std::size_t i = 0; i < exprs.size(); ++i)
        assert(GetMembers()[i] == exprs[i]->GetType()->Decay());

    auto self = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(this, c);
    if (GetMembers().size() == 0)
        return std::move(self);
    std::vector<std::unique_ptr<Expression>> initializers;
    for (std::size_t i = 0; i < exprs.size(); ++i) {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(GetMembers()[i]));
        types.push_back(exprs[i]->GetType());
        auto conset = GetMembers()[i]->GetConstructorOverloadSet(GetAccessSpecifier(c.from, GetMembers()[i]));
        auto call = conset->Resolve(types, c.from);
        if (!call) conset->IssueResolutionError(types, c);
        initializers.push_back(call->Call(Expressions(PrimitiveAccessMember(Wide::Memory::MakeUnique<ExpressionReference>(self.get()), i), std::move(exprs[i])), c));
    }

    struct LambdaConstruction : Expression {
        LambdaConstruction(std::unique_ptr<Expression> self, std::vector<std::unique_ptr<Expression>> inits)
        : self(std::move(self)), inits(std::move(inits)) {}
        std::vector<std::unique_ptr<Expression>> inits;
        std::unique_ptr<Expression> self;
        Type* GetType() override final {
            return self->GetType()->analyzer.GetRvalueType(self->GetType()->Decay());
        }
        llvm::Value* ComputeValue(CodegenContext& con) override final {
            for (auto&& init : inits)
                init->GetValue(con);
            if (self->GetType()->IsComplexType(con))
                con.Destructors.push_back(self.get());
            return self->GetValue(con);
        }
    };

    return Wide::Memory::MakeUnique<LambdaConstruction>(std::move(self), std::move(initializers));
}

bool TupleType::IsA(Type* self, Type* other, Lexer::Access access) {
    auto udt = dynamic_cast<TupleInitializable*>(other);
    if (!udt) return Type::IsA(self, other, access);
    auto udt_members = udt->GetTypesForTuple();
    if (!udt_members) return false;
    if (GetMembers().size() != udt_members->size()) return false;
    bool is = true;
    for (std::size_t i = 0; i < GetMembers().size(); ++i) {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(udt_members->at(i)));
        types.push_back(IsLvalueType(self) ? analyzer.GetLvalueType(GetMembers()[i]) : analyzer.GetRvalueType(GetMembers()[i]));
        is = is && udt_members->at(i)->GetConstructorOverloadSet(Lexer::Access::Public)->Resolve(types, this);
    }
    return is;
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