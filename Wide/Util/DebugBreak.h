#pragma once

namespace Wide {
    namespace Util {
        inline void DebugBreak() {
#ifdef _MSC_VER
            __debugbreak();
#else
#ifdef __clang__
            __builtin_debugtrap();
#else
#ifdef __GNUC__
            __asm__ volatile("int $0x03");
#else
#error "No implementation of DebugBreak() provided!"
#endif
#endif
#endif
        }
    }
}
