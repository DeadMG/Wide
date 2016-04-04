#include <Wide/Semantic/Functions/DefaultedAssignmentOperator.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Reference.h>

using namespace Wide;
using namespace Semantic;
using namespace Functions;

void DefaultedAssignmentOperator::ComputeBody() {
    if (func->args.size() != 1)
        throw std::runtime_error("Bad defaulted function.");
    auto argty = dynamic_cast<ConstructorType*>(analyzer.AnalyzeExpression(context, func->args[0].non_nullable_type.get(), nullptr)->GetType());
    if (!argty)
        throw std::runtime_error("Bad defaulted function.");
    auto local_context = Context{ context, fun->where };
    if (argty->GetConstructedType() == analyzer.GetLvalueType(GetNonstaticMemberContext())) {
        // Copy assignment- copy all.
        unsigned i = 0;
        for (auto&& x : members) {
            root_scope->active.push_back(Type::BuildBinaryExpression(AccessMember(LookupLocal("this"), x, local_context), AccessMember(parameters[1], x, local_context), &Lexer::TokenTypes::Assignment, local_context));
        }
    } else if (argty->GetConstructedType() == analyzer.GetRvalueType(GetNonstaticMemberContext())) {
        // move assignment- move all.
        unsigned i = 0;
        for (auto&& x : members) {
            root_scope->active.push_back(Type::BuildBinaryExpression(AccessMember(LookupLocal("this"), x, local_context), AccessMember(std::make_shared<RvalueCast>(parameters[1]), x, local_context), &Lexer::TokenTypes::Assignment, local_context));
        }
    } else
        throw std::runtime_error("Bad defaulted function.");
}
std::shared_ptr<Expression> DefaultedAssignmentOperator::AccessMember(std::shared_ptr<Expression> self, ConstructorContext::member& member, Context c) {
    if (member.name)
        return Type::AccessMember(self, *member.name, c);
    return Type::AccessBase(self, member.t);
}
DefaultedAssignmentOperator::DefaultedAssignmentOperator(const Parse::Function* astfun, Analyzer& a, Location l, std::vector<ConstructorContext::member> members)
    : FunctionSkeleton(astfun, a, l), func(astfun), members(std::move(members)) {}