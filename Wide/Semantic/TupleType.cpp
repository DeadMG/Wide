#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Codegen/Generator.h>

using namespace Wide;
using namespace Semantic;

TupleType::TupleType(std::vector<Type*> types, Analyzer& a)
: contents(std::move(types)), AggregateType(a) {}

std::unique_ptr<Expression> TupleType::ConstructFromLiteral(std::vector<std::unique_ptr<Expression>> exprs, Context c) {
    assert(exprs.size() == GetContents().size());
    for (std::size_t i = 0; i < exprs.size(); ++i)
        assert(GetContents()[i] == exprs[i]->GetType()->Decay());

    auto self = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(this, c);
    if (GetContents().size() == 0)
        return std::move(self);
    std::vector<std::unique_ptr<Expression>> initializers;
    for (std::size_t i = 0; i < exprs.size(); ++i) {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(GetContents()[i]));
        types.push_back(exprs[i]->GetType());
        auto conset = GetContents()[i]->GetConstructorOverloadSet(GetAccessSpecifier(c.from, GetContents()[i]));
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
        llvm::Value* ComputeValue(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
            for (auto&& init : inits)
                init->GetValue(g, bb);
            return self->GetValue(g, bb);
        }
        void DestroyExpressionLocals(Codegen::Generator& g, llvm::IRBuilder<>& bb) override final {
            self->DestroyLocals(g, bb);
            for (auto rit = inits.rbegin(); rit != inits.rend(); ++rit)
                (*rit)->DestroyLocals(g, bb);
        }
    };

    return Wide::Memory::MakeUnique<LambdaConstruction>(std::move(self), std::move(initializers));
}

bool TupleType::IsA(Type* self, Type* other, Lexer::Access access) {
    auto udt = dynamic_cast<TupleInitializable*>(other);
    if (!udt) return Type::IsA(self, other, access);
    auto udt_members = udt->GetTypesForTuple();
    if (!udt_members) return false;
    if (GetContents().size() != udt_members->size()) return false;
    bool is = true;
    for (std::size_t i = 0; i < GetContents().size(); ++i) {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(udt_members->at(i)));
        types.push_back(IsLvalueType(self) ? analyzer.GetLvalueType(GetContents()[i]) : analyzer.GetRvalueType(GetContents()[i]));
        is = is && udt_members->at(i)->GetConstructorOverloadSet(Lexer::Access::Public)->Resolve(types, this);
    }
    return is;
}
std::string TupleType::explain() {
    std::string name = "{ ";
    for (auto& ty : GetContents()) {
        if (&ty != &GetContents().back()) {
            name += ty->explain() + ", ";
        } else
            name += ty->explain();
    }
    name += " }";
    return name;
}