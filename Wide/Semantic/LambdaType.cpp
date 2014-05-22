#include <Wide/Semantic/LambdaType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/Expression.h>
#include <sstream>
#include <Wide/Parser/AST.h>

using namespace Wide;
using namespace Semantic;

std::vector<Type*> GetTypesFrom(std::vector<std::pair<std::string, Type*>>& vec) {
    std::vector<Type*> out;
    for (auto cap : vec)
        out.push_back(cap.second);
    return out;
}
LambdaType::LambdaType(std::vector<std::pair<std::string, Type*>> capturetypes, const AST::Lambda* l, Analyzer& a)
    : contents(GetTypesFrom(capturetypes)), lam(l), AggregateType(a)
{
    std::size_t i = 0;
    for (auto pair : capturetypes)
        names[pair.first] = i++;
}

std::unique_ptr<Expression> LambdaType::BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) {
    if (val->GetType() == this)
        val = BuildLvalueConstruction(Expressions(std::move(val)), c);
    args.insert(args.begin(), std::move(val));
    std::vector<Type*> types;
    for (auto&& arg : args)
        types.push_back(arg->GetType());
    auto overset = analyzer.GetOverloadSet(analyzer.GetCallableForFunction(lam, args[0]->GetType(), GetNameForOperator(Lexer::TokenType::OpenBracket)));
    auto call = overset->Resolve(types, c.from);
    if (!call) overset->IssueResolutionError(types);
    return call->Call(std::move(args), c);
}
std::unique_ptr<Expression> LambdaType::BuildLambdaFromCaptures(std::vector<std::unique_ptr<Expression>> exprs, Context c) {
    if (GetContents().size() == 0)
        return std::move(exprs[0]);
    auto self = Wide::Memory::MakeUnique<ImplicitTemporaryExpr>(this, c);
    if (contents.size() == 0)
        return std::move(self);
    std::vector<std::unique_ptr<Expression>> initializers;
    for (std::size_t i = 0; i < exprs.size(); ++i) {
        std::vector<Type*> types;
        types.push_back(analyzer.GetLvalueType(GetContents()[i]));
        types.push_back(exprs[i]->GetType());
        auto conset = GetContents()[i]->GetConstructorOverloadSet(GetAccessSpecifier(c.from, GetContents()[i]));
        auto call = conset->Resolve(types, c.from);
        if (!call) conset->IssueResolutionError(types);
        initializers.push_back(call->Call(Expressions(PrimitiveAccessMember(std::move(self), i), std::move(exprs[i])), c));
    }
    
    struct LambdaConstruction : Expression {
        LambdaConstruction(std::unique_ptr<Expression> self, std::vector<std::unique_ptr<Expression>> inits)
        : self(std::move(self)), inits(std::move(inits)) {}
        std::vector<std::unique_ptr<Expression>> inits;
        std::unique_ptr<Expression> self;
        Type* GetType() override final {
            return self->GetType();
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

std::unique_ptr<Expression> LambdaType::LookupCapture(std::unique_ptr<Expression> self, std::string name) {
    if (names.find(name) != names.end())
        return PrimitiveAccessMember(std::move(self), names[name]);
    return nullptr;
}
std::string LambdaType::explain() {
    std::stringstream strstream;
    strstream << this;
    return "(lambda instantiation " + strstream.str() + " at location " + lam->location + ")";
}