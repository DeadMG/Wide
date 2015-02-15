#pragma once

#include <Wide/Semantic/AggregateType.h>

namespace Wide {
    namespace Parse {
        struct Lambda;
    }
    namespace Semantic {
        class FunctionSkeleton;
        class LambdaType : public AggregateType {
            FunctionSkeleton* skeleton;
            std::unordered_map<Parse::Name, std::size_t> names;
            std::vector<Type*> contents;
            std::vector<Type*> GetMembers() { return contents; }
            bool IsNonstaticMemberContext() override final { return true; }
        public:
            Type* GetContext();
            LambdaType(std::vector<std::pair<Parse::Name, Type*>> capturetypes, FunctionSkeleton* skel, Analyzer& a);
            std::shared_ptr<Expression> ConstructCall(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final;
            std::shared_ptr<Expression> BuildLambdaFromCaptures(std::vector<std::shared_ptr<Expression>> exprs, Context c);

            std::shared_ptr<Expression> LookupCapture(std::shared_ptr<Expression> self, Parse::Name name);
            std::string explain() override final;
       };
    }
}