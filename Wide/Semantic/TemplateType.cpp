#include <Wide/Semantic/TemplateType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ConstructorType.h>

using namespace Wide;
using namespace Semantic;

struct TemplateType::TemplateTypeLookupContext : MetaType {
    TemplateTypeLookupContext(Type* t, std::unordered_map<std::string, Type*> args)
    : templatecontext(t), arguments(args) {}
    Type* templatecontext;
    std::unordered_map<std::string, Type*> arguments;
    Wide::Util::optional<ConcreteExpression> AccessMember(ConcreteExpression self, std::string name, Context c) override final {
        if (arguments.find(name) != arguments.end())
            return c->GetConstructorType(arguments[name])->BuildValueConstruction({}, c);
        return templatecontext->AccessMember(self, name, c);
    }
};

TemplateType::TemplateType(const AST::Type* t, Analyzer& a, Type* context, std::unordered_map<std::string, Type*>  arguments)
    : UserDefinedType(t, a, a.arena.Allocate<TemplateTypeLookupContext>(context, arguments)), templatearguments(std::move(arguments)) {}