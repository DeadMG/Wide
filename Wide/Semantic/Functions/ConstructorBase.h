#pragma once

#include <Wide/Semantic/Functions/FunctionSkeleton.h>

namespace Wide {
    namespace Semantic {
        namespace Functions {
            class ConstructorBase : public FunctionSkeleton {
            protected:
                ConstructorBase(const Parse::Constructor* astfun, Analyzer& a, Location, std::vector<ConstructorContext::member>);
                std::vector<ConstructorContext::member> members;
                const Parse::Constructor* constructor;

                const Parse::VariableInitializer* GetInitializer(ConstructorContext::member& member);
                std::shared_ptr<Expression> MakeMember(Type* result, std::function<unsigned()> offset);
                std::shared_ptr<Expression> MakeMemberInitializer(ConstructorContext::member& member, std::vector<std::shared_ptr<Expression>> init, Lexer::Range where);
            };
        }
    }
}