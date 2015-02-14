#include <Wide/Semantic/TemplateType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ConstructorType.h>

using namespace Wide;
using namespace Semantic;

struct TemplateType::TemplateTypeLookupContext : MetaType {
    TemplateTypeLookupContext(Type* t, std::unordered_map<std::string, Type*> args, Analyzer& a)
    : templatecontext(t), arguments(args), MetaType(a) {}
    Type* templatecontext;
    std::unordered_map<std::string, Type*> arguments;
    std::shared_ptr<Expression> AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> self, std::string name, Context c) override final {
        if (arguments.find(name) != arguments.end())
            return arguments[name]->BuildValueConstruction(Expression::NoInstance(), {}, c);
        return Type::AccessMember(Expression::NoInstance(), templatecontext->BuildValueConstruction(Expression::NoInstance(), {}, c), name, c);
    }
    std::string explain() override final {
        return templatecontext->explain();
    }
};

TemplateType::TemplateType(const Parse::Type* t, Analyzer& a, Type* context, std::unordered_map<std::string, Type*> arguments, std::string name)
    : UserDefinedType(t, a, new TemplateTypeLookupContext(context, arguments, a), name), templatearguments(std::move(arguments)), owned_context(static_cast<TemplateTypeLookupContext*>(GetContext())) {}

TemplateType::~TemplateType() {}