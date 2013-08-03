#pragma once

namespace Wide {
    namespace Util {
        void DebugBreak() {
#ifdef _MSC_VER
            __debugbreak();
#else
            throw std::runtime_error("Internal Compiler Error: A problem occurred and the compiler requested the attention of the debugger.");
#endif
        }
    }
}