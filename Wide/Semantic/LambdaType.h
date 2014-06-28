#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace Parse {
        struct Lambda;
    }
    namespace Semantic {
        class LambdaType : public AggregateType, public MemberFunctionContext {
            const Parse::Lambda* lam;
            std::unordered_map<std::string, std::size_t> names;
            std::vector<Type*> contents;
            Type* context;
            std::vector<Type*> GetMembers() { return contents; }
        public:
            Type* GetContext() { return context; }
            LambdaType(std::vector<std::pair<std::string, Type*>> capturetypes, const Parse::Lambda* l, Type* context, Analyzer& a);
            std::unique_ptr<Expression> BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) override final;
            std::unique_ptr<Expression> BuildLambdaFromCaptures(std::vector<std::unique_ptr<Expression>> exprs, Context c);

            std::unique_ptr<Expression> LookupCapture(std::unique_ptr<Expression> self, std::string name);
            std::string explain() override final;
       };
    }
}