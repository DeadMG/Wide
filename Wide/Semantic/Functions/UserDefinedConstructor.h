#pragma once

#include <Wide/Semantic/Functions/ConstructorBase.h>

namespace Wide {
    namespace Semantic {
        namespace Functions {
            class UserDefinedConstructor : public ConstructorBase {
            private:
                bool IsDelegating();
                void ComputeBody();
                void ComputeDelegatedConstructorInitializers();
                void ComputeConstructorInitializers();
            };
        }
    }
}