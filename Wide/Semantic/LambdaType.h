#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace AST {
        struct Lambda;
    }
    namespace Semantic {
        class LambdaType : public AggregateType, public MemberFunctionContext {
            const AST::Lambda* lam;
            std::unordered_map<std::string, std::size_t> names;
            std::vector<Type*> contents;
            const std::vector<Type*>& GetContents() { return contents; }
        public:
            LambdaType(std::vector<std::pair<std::string, Type*>> capturetypes, const AST::Lambda* l, Analyzer& a);
            std::unique_ptr<Expression> BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) override final;
            std::unique_ptr<Expression> BuildLambdaFromCaptures(std::vector<std::unique_ptr<Expression>> exprs, Context c);

            std::unique_ptr<Expression> LookupCapture(std::unique_ptr<Expression> self, std::string name);
            std::string explain() override final;
       };
    }
}