#pragma once
#include <Wide/Semantic/Type.h>
#include <unordered_map>

namespace Wide {
    namespace Semantic {
        class Bool : public PrimitiveType {
            std::unique_ptr<OverloadResolvable> OrAssignOperator;
            std::unique_ptr<OverloadResolvable> XorAssignOperator;
            std::unique_ptr<OverloadResolvable> AndAssignOperator;
            std::unique_ptr<OverloadResolvable> OrOperator;
            std::unique_ptr<OverloadResolvable> AndOperator;
            std::unique_ptr<OverloadResolvable> LTOperator;
            std::unique_ptr<OverloadResolvable> EQOperator;
            std::unique_ptr<OverloadResolvable> NegOperator;
            std::unique_ptr<OverloadResolvable> BooleanConversion;
        public:
            Bool(Analyzer& a) : PrimitiveType(a) {}
            llvm::Type* GetLLVMType(llvm::Module* module) override final;
            Wide::Util::optional<clang::QualType> GetClangType(ClangTU&) override final;
            
            OverloadSet* CreateOperatorOverloadSet(Parse::OperatorName name, Parse::Access access) override final;
            std::size_t size() override final;
            std::size_t alignment() override final;
            std::string explain() override final;
        };
    }
}