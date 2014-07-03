#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/IntegralType.h>
#include <Wide/Semantic/PointerType.h>
#include <Wide/Semantic/Reference.h>
#include <Wide/Semantic/ClangType.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ArrayType.h>
#include <sstream>
#include <Wide/Semantic/Expression.h>

using namespace Wide;
using namespace Semantic;

namespace {
    struct EmplaceType : public MetaType {
        EmplaceType(ConstructorType* con, Analyzer& a)
        : t(con), MetaType(a) {}
        ConstructorType* t;

        std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            auto constructed = t->GetConstructedType();
            if (args.size() == 0)
                throw std::runtime_error("Attempted to emplace a type without providing any memory into which to emplace it.");
            if (args[0]->GetType() != analyzer.GetLvalueType(constructed))
                throw std::runtime_error("Attempted to emplace a T into a space that was not T.lvalue.");
            auto expr = std::move(args.front());
            args.erase(args.begin());
            return Type::BuildInplaceConstruction(std::move(expr), std::move(args), c);
        }

        std::string explain() override final { return t->explain() + ".emplace"; }
    };

    struct Array : public MetaType {
        ConstructorType* t;
        Array(ConstructorType* con, Analyzer& a)
            : MetaType(a), t(con) {}

        std::shared_ptr<Expression> BuildCall(std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            auto constructed = t->GetConstructedType();
            if (args.size() == 0)
                throw std::runtime_error("Attempted to make an array without passing a size.");
            auto integer = dynamic_cast<Wide::Semantic::Integer*>(args[0]->GetImplementation());
            if (!integer)
                throw std::runtime_error("Attempted to make an array but the argument was not an integer.");
            return BuildChain(std::move(self), analyzer.GetConstructorType(analyzer.GetArrayType(t->GetConstructedType(), integer->value.getLimitedValue()))->BuildValueConstruction({}, { this, c.where }));
        }

        std::string explain() override final { return t->explain() + ".array"; }
    };
}

std::shared_ptr<Expression> ConstructorType::BuildCall(std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    assert(val->GetType()->Decay() == this);
    return Wide::Memory::MakeUnique<ExplicitConstruction>(std::move(val), std::move(args), c, t);
}
std::shared_ptr<Expression> ConstructorType::AccessMember(std::shared_ptr<Expression> self, std::string name, Context c) {
    assert(self->GetType()->Decay() == this);
    //return t->AccessStaticMember(name, c);
    if (name == "decay")
        return BuildChain(std::move(self), analyzer.GetConstructorType(t->Decay())->BuildValueConstruction({}, { this, c.where }));
    if (name == "pointer")
        return BuildChain(std::move(self), analyzer.GetConstructorType(analyzer.GetPointerType(t))->BuildValueConstruction({}, { this, c.where }));
    if (name == "size")
        return BuildChain(std::move(self), Wide::Memory::MakeUnique<Integer>(llvm::APInt(64, t->size()), analyzer));
    if (name == "alignment")
        return BuildChain(std::move(self), Wide::Memory::MakeUnique<Integer>(llvm::APInt(64, t->alignment()), analyzer));
    if (t == analyzer.GetVoidType())
        return nullptr;
    if (name == "emplace") {
        if (!emplace) emplace = Wide::Memory::MakeUnique<EmplaceType>(this, analyzer);
        return BuildChain(std::move(self), emplace->BuildValueConstruction({}, { this, c.where }));
    }
    if (name == "lvalue")
        return BuildChain(std::move(self), analyzer.GetConstructorType(analyzer.GetLvalueType(t))->BuildValueConstruction({}, { this, c.where }));
    if (name == "rvalue")
        return BuildChain(std::move(self), analyzer.GetConstructorType(analyzer.GetRvalueType(t))->BuildValueConstruction({}, { this, c.where }));
    // If we're a Clang type, offer constructor and destructor overload sets.
    if (auto clangty = dynamic_cast<ClangType*>(t)) {
        if (name == "constructors") {
            return BuildChain(std::move(self), clangty->GetConstructorOverloadSet(GetAccessSpecifier(c.from, clangty))->BuildValueConstruction({}, c));
        }
        if (name == "destructor") {
            return BuildChain(std::move(self), clangty->GetDestructorOverloadSet()->BuildValueConstruction({}, c));
        }
    }
    // If we're not a reference type, offer array.
    if (!t->IsReference()) {
        if (name == "array") {
            if (!array) array = Wide::Memory::MakeUnique<Array>(this, analyzer);
            return BuildChain(std::move(self), array->BuildValueConstruction({}, { this, c.where }));
        }
    }
    if (name == "trivially_destructible")
        return BuildChain(std::move(self), Wide::Memory::MakeUnique<Boolean>(t->IsTriviallyDestructible(), analyzer));
    if (name == "trivially_copy_constructible")
        return BuildChain(std::move(self), Wide::Memory::MakeUnique<Boolean>(t->IsCopyConstructible(GetAccessSpecifier(c.from, t)) && t->IsTriviallyCopyConstructible(), analyzer));
    if (name == "copy_constructible")
        return BuildChain(std::move(self), Wide::Memory::MakeUnique<Boolean>(t->IsCopyConstructible(GetAccessSpecifier(c.from, t)), analyzer));
    if (name == "copy_assignable")
        return BuildChain(std::move(self), Wide::Memory::MakeUnique<Boolean>(t->IsCopyAssignable(GetAccessSpecifier(c.from, t)), analyzer));
    if (name == "move_constructible")
        return BuildChain(std::move(self), Wide::Memory::MakeUnique<Boolean>(t->IsMoveConstructible(GetAccessSpecifier(c.from, t)), analyzer));
    if (name == "move_assignable")
        return BuildChain(std::move(self), Wide::Memory::MakeUnique<Boolean>(t->IsMoveAssignable(GetAccessSpecifier(c.from, t)), analyzer));
    if (name == "empty")
        return BuildChain(std::move(self), Wide::Memory::MakeUnique<Boolean>(t->IsEmpty(), analyzer));
    return nullptr;
}

ConstructorType::ConstructorType(Type* con, Analyzer& a)
: MetaType(a) {
    t = con;
    assert(t);
    emplace = nullptr;
}
std::string ConstructorType::explain() {
    return "decltype(" + t->explain() + ")";
}