#include <Wide/Util/Driver/IncludePaths.h>
#include <Wide/Semantic/ClangOptions.h>
#include <llvm/Support/Path.h>
#include <boost/algorithm/string.hpp>
/*
/usr/include/c++/4.7
/usr/include/c++/4.7/x86_64-linux-gnu
/usr/include/c++/4.7/backward
/usr/lib/gcc/x86_64-linux-gnu/4.7/include
/usr/local/include
/usr/lib/gcc/x86_64-linux-gnu/4.7/include-fixed
/usr/include/x86_64-linux-gnu
/usr/include
*/
/*
Added include path: /usr/include/c++/4.7
Added include path: /usr/include/c++/4.7/x86_64-linux-gnu
Added include path: /usr/include/c++/4.7/backward
Added include path: /usr/lib/gcc/x86_64-linux-gnu/4.7/include
Added include path: /usr/local/include
Added include path: /usr/lib/gcc/x86_64-linux-gnu/4.7/include-fixed
Added include path: /usr/include/x86_64-linux-gnu
Added include path: /usr/include
*/
// --include="usr/local/lib/gcc/x86_64-unknown-linux-gnu/4.8.2/../../../../include/c++/4.8.2"
// --include="usr/local/lib/gcc/x86_64-unknown-linux-gnu/4.8.2/../../../../include/c++/4.8.2/x86_64-unknown-linux-gnu/"
// --include="usr/local/lib/gcc/x86_64-unknown-linux-gnu/4.8.2/../../../../include/c++/4.8.2/backward"
// --include="usr/local/lib/gcc/x86_64-unknown-linux-gnu/4.8.2/include"
// --include="usr/local/lib/gcc/x86_64-unknown-linux-gnu/4.8.2/include-fixed"
// --include="usr/local/include"
// --include="usr/include/x86_64-linux-gnu"
// --include="usr/include"
// --include="/usr/include/c++/4.7" --include="/usr/include/c++/4.7/x86_64-linux-gnu" --include="/usr/include/c++/4.7/backward" --include="/usr/lib/gcc/x86_64-linux-gnu/4.7/include" --include="/usr/local/include" --include="/usr/lib/gcc/x86_64-linux-gnu/4.7/include-fixed" --include="/usr/include/x86_64-linux-gnu" --include="/usr/include" hello.wide

void Wide::Driver::AddLinuxIncludePaths(Options::Clang& ClangOpts, std::string gccver) {
    auto base = "/usr/lib/gcc/x86_64-unknown-linux-gnu/" + gccver;
    ClangOpts.HeaderSearchOptions->AddPath(base + "/include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(base + "/include-fixed", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath("/usr/include/c++/" + gccver, clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath("/usr/include/c++/" + gccver + "/x86_64-unknown-linux-gnu/", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath("/usr/include/c++/" + gccver + "/backward", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath("/usr/local/include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath("/usr/include/x86_64-linux-gnu", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath("/usr/include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
}
void Wide::Driver::AddLinuxIncludePaths(Options::Clang& ClangOpts, int gnuc, int gnucminor, int gnucpatchlevel) {
    std::string gccver = std::to_string(gnuc) + "." + std::to_string(gnucminor) + "." + std::to_string(gnucpatchlevel);
    return AddLinuxIncludePaths(ClangOpts, gccver);
}

namespace {
    FILE* really_popen(const char* cmd, const char* mode) {
#ifdef _MSC_VER
        return _popen(cmd, mode);
#else
        return popen(cmd, mode);
#endif
    }
    void really_pclose(FILE* pipe) {
#ifdef _MSC_VER
        _pclose(pipe);
#else
        pclose(pipe);
#endif
    }
    std::string ExecuteProcess(const char* cmd) {
        FILE* pipe = really_popen(cmd, "r");
        if (!pipe) throw std::runtime_error("Could not invoke command " + std::string(cmd));
        char buffer[128];
        std::string result = "";
        while(!feof(pipe)) {
            if(fgets(buffer, 128, pipe) != NULL)
                result += buffer;
        }
        really_pclose(pipe);
        return result;
    }
}
void Wide::Driver::AddLinuxIncludePaths(Options::Clang& ClangOpts) {
    auto result = ExecuteProcess("g++ -E -x c++ - -v < /dev/null 2>&1");
    auto begin = boost::algorithm::find_first(result, "#include <...> search starts here:");
    auto end = boost::algorithm::find_first(result, "End of search list.");
    if (!begin || !end)
        throw std::runtime_error("Could not find G++ header search paths in G++ output.");
    auto path_strings = std::string(begin.end(), end.begin());
    std::vector<std::string> paths;
    boost::algorithm::split(paths, path_strings, [](char c) { return c == '\n'; });
    for(auto&& path : paths) {
        boost::algorithm::trim(path);
        if (path.empty()) continue;
        ClangOpts.HeaderSearchOptions->AddPath(path, clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    }
}
void Wide::Driver::AddMinGWIncludePaths(Options::Clang& ClangOpts, std::string MinGWInstallPath) {
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\include\\c++\\4.6.3", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\include\\c++\\4.6.3\\i686-w64-mingw32", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\i686-w64-mingw32\\include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
}
