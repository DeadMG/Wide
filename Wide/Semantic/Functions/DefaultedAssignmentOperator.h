#pragma once

#include <Wide/Semantic/Functions/FunctionSkeleton.h>

namespace Wide {
    namespace Semantic {
        namespace Functions {
            class DefaultedAssignmentOperator : public FunctionSkeleton {
                std::vector<ConstructorContext::member> members;
                const Parse::Function* func;
                void ComputeBody();
                std::shared_ptr<Expression> AccessMember(std::shared_ptr<Expression> self, ConstructorContext::member& member, Context c);
            public:
                DefaultedAssignmentOperator(const Parse::Function* astfun, Analyzer& a, Location, std::vector<ConstructorContext::member>);
            };
        }
    }
}
