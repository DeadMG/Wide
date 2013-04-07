#include "ConstructorType.h"

using namespace Wide;
using namespace Semantic;

Expression ConstructorType::BuildCall(Expression, std::vector<Expression> args, Analyzer& a) {
    return t->BuildRvalueConstruction(std::move(args), a);
}
Expression ConstructorType::AccessMember(Expression, std::string name, Analyzer& a) {
    Expression self;
    self.t = t;
    self.Expr = nullptr;
    return t->AccessMember(self, name, a);
}