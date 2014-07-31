#pragma once

#include <Wide/Semantic/ABI.h>

namespace Wide {
    namespace Semantic {
        class Itanium : public ABI {
        public:
            llvm::CallingConv::ID GetDefaultCallingConvention();
            std::string GetEHPersonality();
        };
    }
}