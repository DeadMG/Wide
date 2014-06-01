#include <Wide/Semantic/Warnings/GetFunctionName.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Parser/AST.h>

std::string Wide::Semantic::GetFunctionName(const AST::FunctionBase* func, Analyzer& a, std::string context, Wide::Semantic::Module* root) {
    context += "(";
    for (auto&& arg : func->args) {
        context += arg.name;
        if (arg.type) {
            struct autotype : public Wide::Semantic::MetaType {
                autotype(Type* con) : context(con), MetaType(con->analyzer) {}
                std::unique_ptr<ConstructorType> conty;
                std::unique_ptr<Expression> AccessMember(std::unique_ptr<Expression> self, std::string name, Context c) override final {
                    if (name == "auto") {
                        if (!conty) conty = Wide::Memory::MakeUnique<ConstructorType>(this, analyzer);
                        return conty->BuildValueConstruction(Expressions(), c);
                    }
                    return nullptr;
                }
                Type* context;
                Type* GetContext() override final {
                    return context;
                }
                std::string explain() {
                    return "auto";
                }
            };
            try {
                auto autoty = autotype(root);
                auto expr = AnalyzeExpression(&autoty, arg.type, a);
                auto&& conty = dynamic_cast<ConstructorType&>(*expr->GetType()->Decay());
                context += " := " + conty.GetConstructedType()->explain();
            }
            catch (...) {
                context += ":= error-type";
            }
        }
        if (&arg != &func->args.back())
            context += ", ";
    }
    return context + ")";
}