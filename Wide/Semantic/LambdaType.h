#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace Parse {
        struct Lambda;
    }
    namespace Semantic {
        class LambdaType : public AggregateType, public MemberFunctionContext {
            const Parse::Lambda* lam;
            std::unordered_map<Parse::Name, std::size_t> names;
            std::vector<Type*> contents;
            Type* context;
            std::vector<Type*> GetMembers() { return contents; }
        public:
            Type* GetContext() { return context; }
            LambdaType(std::vector<std::pair<Parse::Name, Type*>> capturetypes, const Parse::Lambda* l, Type* context, Analyzer& a);
            std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            std::shared_ptr<Expression> BuildLambdaFromCaptures(std::vector<std::shared_ptr<Expression>> exprs, Context c);

            std::shared_ptr<Expression> LookupCapture(std::shared_ptr<Expression> self, Parse::Name name);
            std::string explain() override final;
       };
    }
}