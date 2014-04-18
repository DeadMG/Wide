#pragma once

#include <string>

namespace Wide {
    namespace Options {
        struct Clang;
    }
    namespace Driver {
        void AddLinuxIncludePaths(Options::Clang& clangopts, int gnuc, int gnucminor, int gnucpatchlevel);
        void AddLinuxIncludePaths(Options::Clang& clangopts, std::string gccver); 
        void AddLinuxIncludePaths(Options::Clang& ClangOpts);
        void AddMinGWIncludePaths(Options::Clang& clangopts, std::string MinGWBase);
    }
}