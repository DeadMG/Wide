#pragma once

#include <Wide/Semantic/Functions/ConstructorBase.h>

namespace Wide {
    namespace Semantic {
        namespace Functions {
            class DefaultedConstructor : public ConstructorBase {
                void ComputeBody();
                void ComputeDefaultConstructorBody();
                void ComputeCopyConstructorBody();
                void ComputeMoveConstructorBody();
                std::shared_ptr<Expression> GetMemberInitializer(ConstructorContext::member& member, std::shared_ptr<Expression> _this);
            };
        }
    }
}