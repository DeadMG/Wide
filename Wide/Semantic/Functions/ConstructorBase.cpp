#include <Wide/Semantic/Functions/ConstructorBase.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Reference.h>

using namespace Wide;
using namespace Semantic;
using namespace Functions;

const Parse::VariableInitializer* ConstructorBase::GetInitializer(ConstructorContext::member& member) {
    for (auto&& init : constructor->initializers) {
        // Match if it's a name and the one we were looking for.
        auto ident = dynamic_cast<const Parse::Identifier*>(init.initialized.get());
        if (member.name && ident) {
            if (auto string = boost::get<std::string>(&ident->val.name))
                if (*string == *member.name)
                    return &init;
        } else {
            // Match if it's a type and the one we were looking for.
            auto ty = analyzer.AnalyzeExpression(GetLocalContext(), init.initialized.get(), LookupLocal("this"));
            if (auto conty = dynamic_cast<ConstructorType*>(ty->GetType())) {
                if (conty->GetConstructedType() == member.t)
                    return &init;
            }
        }
    }
    return nullptr;
}
std::shared_ptr<Expression> ConstructorBase::MakeMember(Type* result, std::function<unsigned()> offset) {
    // Differs from PrimitiveAccessMember and friends because we always return a reference to T, regardless of if T is a reference or not.
    return CreatePrimUnOp(LookupLocal("this"), result, [offset, result](llvm::Value* val, CodegenContext& con) -> llvm::Value* {
        auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
        self = con->CreateConstGEP1_32(self, offset());
        return con->CreatePointerCast(self, result->GetLLVMType(con));
    });
}
std::shared_ptr<Expression> ConstructorBase::MakeMemberInitializer(ConstructorContext::member& member, std::vector<std::shared_ptr<Expression>> init, Lexer::Range where) {
    auto memberexpr = MakeMember(analyzer.GetLvalueType(member.t), member.num);
    auto construction = Type::BuildInplaceConstruction(memberexpr, std::move(init), { context, where });
    auto destructor = member.t->IsTriviallyDestructible()
        ? std::function<void(CodegenContext&)>()
        : member.t->BuildDestructorCall(memberexpr, { context, where }, true);
    return CreatePrimGlobal(Range::Elements(construction), analyzer.GetVoidType(), [=](CodegenContext& con) {
        construction->GetValue(con);
        if (destructor)
            con.AddExceptionOnlyDestructor(destructor);
        return nullptr;
    });
}