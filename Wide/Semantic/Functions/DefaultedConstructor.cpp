#include <Wide/Semantic/Functions/DefaultedConstructor.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Reference.h>

using namespace Wide;
using namespace Semantic;
using namespace Functions;

void DefaultedConstructor::ComputeBody() {
    if (constructor->args.size() == 0) 
        return ComputeDefaultConstructorBody();
    if (constructor->args.size() != 1)
        throw std::runtime_error("Fuck");
    // A single-argument constructor.
    if (auto argconty = dynamic_cast<ConstructorType*>(analyzer.AnalyzeExpression(context, constructor->args[0].non_nullable_type.get(), nullptr)->GetType())) {
        if (argconty->GetConstructedType() == analyzer.GetLvalueType(GetNonstaticMemberContext()))
            return ComputeCopyConstructorBody();
        else if (argconty->GetConstructedType() == analyzer.GetRvalueType(GetNonstaticMemberContext()))
            return ComputeMoveConstructorBody();
    }
    throw std::runtime_error("Fuck");
}
void DefaultedConstructor::ComputeDefaultConstructorBody() {
    // Default-or-NSDMI all the things.
    for (auto&& x : members) {
        auto member = MakeMember(analyzer.GetLvalueType(x.t), x.num);
        std::vector<std::shared_ptr<Expression>> inits;
        if (x.InClassInitializer)
            inits = { x.InClassInitializer(LookupLocal("this")) };
        root_scope->active.push_back(Type::BuildInplaceConstruction(member, inits, { GetLocalContext(), constructor->where }));
    }
    root_scope->active.push_back(Type::SetVirtualPointers(LookupLocal("this"), GetLocalContext()));
}
void DefaultedConstructor::ComputeCopyConstructorBody() {
    unsigned i = 0;
    for (auto&& x : members) {
        auto member_ref = MakeMember(analyzer.GetLvalueType(x.t), x.num);
        root_scope->active.push_back(GetMemberInitializer(x, parameters[1]));
    }
    root_scope->active.push_back(Type::SetVirtualPointers(LookupLocal("this"), GetLocalContext()));
}
void DefaultedConstructor::ComputeMoveConstructorBody() {
    unsigned i = 0;
    for (auto&& x : members) {
        auto member_ref = MakeMember(analyzer.GetLvalueType(x.t), x.num);
        root_scope->active.push_back(GetMemberInitializer(x, std::make_shared<RvalueCast>(parameters[1])));
    }
    root_scope->active.push_back(Type::SetVirtualPointers(LookupLocal("this"), GetLocalContext()));
}
std::shared_ptr<Expression> DefaultedConstructor::GetMemberInitializer(ConstructorContext::member& member, std::shared_ptr<Expression> other) {
    if (member.name)
        return MakeMemberInitializer(member, { Type::AccessMember(other, *member.name, { GetLocalContext(), constructor->where }) }, constructor->where);
    return MakeMemberInitializer(member, { Type::AccessBase(other, member.t) }, constructor->where);
}