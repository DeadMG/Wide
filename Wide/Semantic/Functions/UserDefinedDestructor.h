#pragma once

#include <Wide/Semantic/Functions/FunctionSkeleton.h>

namespace Wide {
    namespace Semantic {
        namespace Functions {
            class UserDefinedDestructor : public FunctionSkeleton {
                std::vector<ConstructorContext::member> members;
                void ComputeBody();
            };
        }
    }
}