#pragma once

#include <memory>

namespace Wide {
    namespace Memory {
        template<typename T, typename... Args> std::unique_ptr<T> MakeUnique(Args&&... args) {
            return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
        }
    }
}