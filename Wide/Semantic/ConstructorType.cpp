#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Codegen/Generator.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Reference.h>
#include <sstream>

using namespace Wide;
using namespace Semantic;

struct EmplaceType : public MetaType {
    EmplaceType(ConstructorType* con, Analyzer& a)
        : t(con), MetaType(a) {}
    ConstructorType* t;

    std::unique_ptr<Expression> BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) override final {
        auto constructed = t->GetConstructedType();
        if (args.size() == 0)
            throw std::runtime_error("Attempted to emplace a type without providing any memory into which to emplace it.");
        if (args[0]->GetType()->Decay() != analyzer.GetPointerType(constructed))
            throw std::runtime_error("Attempted to emplace a T into a type that was not a pointer to T.");
        auto expr = std::move(args.front());
        args.erase(args.begin());
        return constructed->BuildInplaceConstruction(std::move(expr), std::move(args), c);
    }

    std::string explain() override final { return t->explain() + ".emplace"; }
};

std::unique_ptr<Expression> ConstructorType::BuildCall(std::unique_ptr<Expression> val, std::vector<std::unique_ptr<Expression>> args, Context c) {
    assert(val->GetType()->Decay() == this);
    return t->BuildValueConstruction(std::move(args), c);
}
std::unique_ptr<Expression> ConstructorType::AccessMember(std::unique_ptr<Expression> self, std::string name, Context c) {
    assert(self->GetType()->Decay() == this);
    //return t->AccessStaticMember(name, c);
    if (name == "decay")
        return analyzer.GetConstructorType(t->Decay())->BuildValueConstruction({}, { this, c.where });
    if (name == "pointer")
        return analyzer.GetConstructorType(analyzer.GetPointerType(t))->BuildValueConstruction({}, { this, c.where });
    if (name == "size")
        return Wide::Memory::MakeUnique<Integer>(llvm::APInt(64, t->size()), analyzer);
    if (name == "alignment")
        return Wide::Memory::MakeUnique<Integer>(llvm::APInt(64, t->alignment()), analyzer);
    if (t == analyzer.GetVoidType())
        return nullptr;
    if (name == "emplace") {
        if (!emplace) emplace = Wide::Memory::MakeUnique<EmplaceType>(this, analyzer);
        return emplace->BuildValueConstruction({}, { this, c.where });
    }
    if (name == "lvalue")
        return analyzer.GetConstructorType(analyzer.GetLvalueType(t))->BuildValueConstruction({}, { this, c.where });
    if (name == "rvalue")
        return analyzer.GetConstructorType(analyzer.GetRvalueType(t))->BuildValueConstruction({}, { this, c.where });
    return nullptr;
}

ConstructorType::ConstructorType(Type* con, Analyzer& a)
: MetaType(a) {
    t = con;
    emplace = nullptr;
}
std::string ConstructorType::explain() {
    return "decltype(" + t->explain() + ")";
}