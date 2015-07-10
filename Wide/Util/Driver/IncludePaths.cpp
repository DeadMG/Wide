#include <Wide/Util/Driver/IncludePaths.h>
#include <Wide/Semantic/ClangOptions.h>
#include <llvm/Support/Path.h>
#include <boost/algorithm/string.hpp>
#include <Wide/Util/Paths/Append.h>
#include <Wide/Util/Paths/Exists.h>

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
namespace {
    void AddPath(Wide::Options::Clang& clangopts, std::string path) {
        if (!Wide::Paths::Exists(path))
            throw std::runtime_error("Could not find include path " + path);
        clangopts.HeaderSearchOptions->AddPath(path, clang::frontend::IncludeDirGroup::CXXSystem, false, false);
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
        AddPath(ClangOpts, path);
    }
}
void Wide::Driver::AddMinGWIncludePaths(Options::Clang& ClangOpts, std::string MinGWInstallPath) {
    AddPath(ClangOpts, Wide::Paths::Append(MinGWInstallPath, "include\\c++\\4.8.0"));
    AddPath(ClangOpts, Wide::Paths::Append(MinGWInstallPath, "include\\c++\\4.8.0\\i686-w64-mingw32"));
    AddPath(ClangOpts, Wide::Paths::Append(MinGWInstallPath, "i686-w64-mingw32\\include"));
}
