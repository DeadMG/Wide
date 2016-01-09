#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <Wide/Util/Driver/Compile.h>

namespace Wide {
    namespace Options {
        struct Clang;
    }
    namespace Driver {
        void TestDirectory(std::string path, std::string mode, std::string program, bool debugbreak, std::unordered_map<std::string, std::function<bool()>>& failed);
        void Jit(Wide::Options::Clang& copts, std::string file);
        void Compile(Wide::Options::Clang& copts, std::string file);
    }
}
