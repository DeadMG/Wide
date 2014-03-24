#pragma once

#include <Wide/Semantic/UserDefinedType.h>

namespace Wide {
    namespace Semantic {
        class TemplateType : public UserDefinedType {
            struct TemplateTypeLookupContext;
            std::unordered_map<std::string, Type*> templatearguments;
        public:
            TemplateType(const AST::Type* t, Analyzer& a, Type* context, std::unordered_map<std::string, Type*> arguments, std::string name);
            std::unordered_map<std::string, Type*>  GetTemplateArguments() { return templatearguments; }
        };
    }
}