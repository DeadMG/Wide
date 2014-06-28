#pragma once

#include <Wide/Semantic/UserDefinedType.h>

namespace Wide {
    namespace Semantic {
        class TemplateType : public UserDefinedType {
            struct TemplateTypeLookupContext;
            std::unique_ptr<TemplateTypeLookupContext> owned_context;
            std::unordered_map<std::string, Type*> templatearguments;
        public:
            ~TemplateType();
            TemplateType(const Parse::Type* t, Analyzer& a, Type* context, std::unordered_map<std::string, Type*> arguments, std::string name);
            std::unordered_map<std::string, Type*>  GetTemplateArguments() { return templatearguments; }
        };
    }
}