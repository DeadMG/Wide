#pragma once

#include <string>

#pragma warning(push, 0)
#include <llvm/IR/CallingConv.h>
#pragma warning(pop)

namespace Wide {
    namespace Semantic {
        struct ABI {
            virtual llvm::CallingConv::ID GetDefaultCallingConvention() = 0;
            virtual std::string GetEHPersonality() = 0;
            virtual ~ABI() {}
        };
    }
}