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
    struct Array : public MetaType {
        ConstructorType* t;
        Array(ConstructorType* con, Analyzer& a)
            : MetaType(a), t(con) {}

        std::shared_ptr<Expression> ConstructCall(Expression::InstanceKey key, std::shared_ptr<Expression> self, std::vector<std::shared_ptr<Expression>> args, Context c) override final {
            if (args.size() != 1)
                throw SpecificError<ArrayWrongArgumentNumber>(analyzer, c.where, "Attempted to make an array with wrong number of arguments.");
            auto constructed = t->GetConstructedType();
            if (!args[0]->IsConstantExpression(key))
                throw SpecificError<ArrayNotConstantSize>(analyzer, c.where, "Attempted to make an array but the size was not a constant integer.");
            auto integer = analyzer.EvaluateConstantIntegerExpression(args[0], key);
            return BuildChain(std::move(self), analyzer.GetConstructorType(analyzer.GetArrayType(t->GetConstructedType(), integer.getLimitedValue()))->BuildValueConstruction(Expression::NoInstance(), {}, { this, c.where }));
        }

        std::string explain() override final { return t->explain() + ".array"; }
    };
    struct Member : public MetaType {
        ConstructorType* t;
        Member(ConstructorType* con, Analyzer& a)
            : t(con), MetaType(a) {}

        std::string explain() override final { return t->explain() + ".members"; }
        bool IsLookupContext() { return true; }
        std::shared_ptr<Expression> AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> self, std::string name, Context c) override final {
            auto member = t->GetConstructedType()->AccessStaticMember(name, c);
            if (!member)
                return nullptr;
            return BuildChain(self, member);
        }
        OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName what, Parse::Access access, OperatorAccess kind) {
            if (kind == OperatorAccess::Implicit)
                return Type::CreateOperatorOverloadSet(what, access, kind);
            return t->GetConstructedType()->AccessMember(what, access, OperatorAccess::Explicit);
        }
    };
}

std::shared_ptr<Expression> ConstructorType::ConstructCall(Expression::InstanceKey key, std::shared_ptr<Expression> val, std::vector<std::shared_ptr<Expression>> args, Context c) {
    return BuildChain(val, t->BuildValueConstruction(key, args, c));
}
std::shared_ptr<Expression> ConstructorType::AccessNamedMember(Expression::InstanceKey key, std::shared_ptr<Expression> self, std::string name, Context c) {
    //return t->AccessStaticMember(name, c);
    if (name == "decay")
        return BuildChain(std::move(self), analyzer.GetConstructorType(t->Decay())->BuildValueConstruction(key, {}, { this, c.where }));
    if (name == "pointer")
        return BuildChain(std::move(self), analyzer.GetConstructorType(analyzer.GetPointerType(t))->BuildValueConstruction(key, {}, { this, c.where }));
    if (name == "size")
        return BuildChain(std::move(self), Wide::Memory::MakeUnique<Integer>(llvm::APInt(64, t->size()), analyzer));
    if (name == "alignment")
        return BuildChain(std::move(self), Wide::Memory::MakeUnique<Integer>(llvm::APInt(64, t->alignment()), analyzer));
    if (t == analyzer.GetVoidType())
        return nullptr;
    if (name == "lvalue")
        return BuildChain(std::move(self), analyzer.GetConstructorType(analyzer.GetLvalueType(t))->BuildValueConstruction(key, {}, { this, c.where }));
    if (name == "rvalue")
        return BuildChain(std::move(self), analyzer.GetConstructorType(analyzer.GetRvalueType(t))->BuildValueConstruction(key, {}, { this, c.where }));
    // If we're a Clang type, offer constructor and destructor overload sets.
    if (name == "constructors") {
        return BuildChain(std::move(self), t->GetConstructorOverloadSet(GetAccessSpecifier(c.from, t))->BuildValueConstruction(key, {}, c));
    }
    if (auto clangty = dynamic_cast<ClangType*>(t)) {
        if (name == "destructor") {
            return BuildChain(std::move(self), clangty->GetDestructorOverloadSet()->BuildValueConstruction(key, {}, c));
        }
    }
    // If we're not a reference type, offer array.
    if (!t->IsReference()) {
        if (name == "array") {
            if (!array) array = Wide::Memory::MakeUnique<Array>(this, analyzer);
            return BuildChain(std::move(self), array->BuildValueConstruction(key, {}, { this, c.where }));
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
    if (name == "members") {
        if (!members) members = Wide::Memory::MakeUnique<Member>(this, analyzer);
        return BuildChain(std::move(self), members->BuildValueConstruction(key, {}, { this, c.where }));
    }
    return nullptr;
}

ConstructorType::ConstructorType(Type* con, Analyzer& a)
: MetaType(a) {
    t = con;
    assert(t);
}
std::string ConstructorType::explain() {
    return "decltype(" + t->explain() + ")";
}