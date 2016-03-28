#include <Wide/Semantic/Functions/UserDefinedDestructor.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ConstructorType.h>
#include <Wide/Semantic/Reference.h>

using namespace Wide;
using namespace Semantic;
using namespace Functions;

void UserDefinedDestructor::ComputeBody() {
    FunctionSkeleton::ComputeBody();

    for (auto rit = members.rbegin(); rit != members.rend(); ++rit) {
        auto num = rit->num;
        auto result = analyzer.GetLvalueType(rit->t);
        auto member = CreatePrimUnOp(LookupLocal("this"), result, [num, result](llvm::Value* val, CodegenContext& con) -> llvm::Value* {
            auto self = con->CreatePointerCast(val, con.GetInt8PtrTy());
            self = con->CreateConstGEP1_32(self, num());
            return con->CreatePointerCast(self, result->GetLLVMType(con));
        });
        auto destructor = rit->t->BuildDestructorCall(member, { context, fun->where }, true);
        root_scope->active.push_back(CreatePrimGlobal(Range::Empty(), analyzer, destructor));
    }
}