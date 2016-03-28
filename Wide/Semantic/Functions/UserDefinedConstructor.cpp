#include <Wide/Semantic/Functions/UserDefinedConstructor.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Reference.h>

using namespace Wide;
using namespace Semantic;
using namespace Functions;

bool UserDefinedConstructor::IsDelegating() {
    if (constructor->initializers.size() != 1)
        return false;
    if (auto ident = dynamic_cast<const Parse::Identifier*>(constructor->initializers.front().initialized.get())) {
        if (ident->val == Parse::Name("type")) {
            return true;
        }
    }
    return false;
};
void UserDefinedConstructor::ComputeDelegatedConstructorInitializers() {
    Context c{ context, constructor->initializers.front().where };
    auto expr = analyzer.AnalyzeExpression(GetLocalContext(), constructor->initializers.front().initializer.get(), LookupLocal("this"));
    auto conty = GetNonstaticMemberContext();
    auto conoverset = conty->GetConstructorOverloadSet(Parse::Access::Private);
    auto destructor = conty->BuildDestructorCall(LookupLocal("this"), c, true);
    auto self = LookupLocal("this");
    auto callable = conoverset->Resolve({ self->GetType(), expr->GetType() }, context);
    if (!callable) throw std::runtime_error("Couldn't resolve delegating constructor call.");
    auto call = callable->Call({ LookupLocal("this"), expr }, c);
    root_scope->active.push_back(BuildChain(call, CreatePrimGlobal(Range::Elements(self, expr), analyzer.GetVoidType(), [=](CodegenContext& con) {
        if (!self->GetType()->Decay()->IsTriviallyDestructible())
            con.AddExceptionOnlyDestructor(destructor);
        return nullptr;
    })));
    // Don't bother setting virtual pointers as the delegated-to constructor already did.
}
void UserDefinedConstructor::ComputeConstructorInitializers() {
    std::unordered_set<const Parse::VariableInitializer*> used_initializers;
    for (auto&& x : members) {
        auto result = analyzer.GetLvalueType(x.t);
        // Gotta get the correct this pointer.
        if (auto init = GetInitializer(x)) {
            // AccessMember will automatically give us back a T*, but we need the T** here
            // if the type of this member is a reference.
            used_initializers.insert(init);
            if (init->initializer) {
                // If it's a tuple, pass each subexpression.
                std::vector<std::shared_ptr<Expression>> exprs;
                if (auto tup = dynamic_cast<const Parse::Tuple*>(init->initializer.get())) {
                    for (auto&& expr : tup->expressions)
                        exprs.push_back(analyzer.AnalyzeExpression(GetLocalContext(), expr.get(), LookupLocal("this")));
                } else
                    exprs.push_back(analyzer.AnalyzeExpression(GetLocalContext(), init->initializer.get(), LookupLocal("this")));
                root_scope->active.push_back(MakeMemberInitializer(x, std::move(exprs), init->where));
            } else
                root_scope->active.push_back(MakeMemberInitializer(x, {}, init->where));
            continue;
        }
        // Don't care about if x.t is ref because refs can't be default-constructed anyway.
        if (x.InClassInitializer) {
            root_scope->active.push_back(MakeMemberInitializer(x, { x.InClassInitializer(LookupLocal("this")) }, x.location));
            continue;
        }
        root_scope->active.push_back(MakeMemberInitializer(x, {}, fun->where));
    }
    for (auto&& x : constructor->initializers) {
        if (used_initializers.find(&x) == used_initializers.end()) {
            if (auto ident = dynamic_cast<const Parse::Identifier*>(x.initialized.get())) {
                InitializerErrors[&x] = Wide::Memory::MakeUnique<Semantic::SpecificError<NoMemberToInitialize>>(analyzer, x.where, "Identifier " + Semantic::GetNameAsString(ident->val) + " was not a member.");
                continue;
            }
            auto expr = analyzer.AnalyzeExpression(GetLocalContext(), x.initializer.get(), LookupLocal("this"));
            auto conty = dynamic_cast<ConstructorType*>(expr->GetType());
            if (!conty) {
                InitializerErrors[&x] = Wide::Memory::MakeUnique<Semantic::SpecificError<InitializerNotType>>(analyzer, x.where, "Initializer did not resolve to a type.");
                continue;
            }
            InitializerErrors[&x] = Wide::Memory::MakeUnique<Semantic::SpecificError<NoBaseToInitialize>>(analyzer, x.where, "Initialized type was not a direct base class.");
        }
    }
    root_scope->active.push_back(Type::SetVirtualPointers(LookupLocal("this"), context));
}
void UserDefinedConstructor::ComputeBody() {
    if (IsDelegating())
        ComputeDelegatedConstructorInitializers();
    else
        ComputeConstructorInitializers();
    FunctionSkeleton::ComputeBody();
}