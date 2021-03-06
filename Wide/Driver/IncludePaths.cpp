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
    std::string ExecuteProcess(std::string cmd) {
        FILE* pipe = really_popen(cmd.c_str(), "r");
        if (!pipe) throw std::runtime_error("Could not invoke command " + cmd);
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
std::vector<std::string> Wide::Driver::GetGCCIncludePaths(std::string gcc) {
#ifdef _MSC_VER
    auto result = ExecuteProcess(gcc + " -E -x c++ - -v < NUL 2>&1");
#else
    auto result = ExecuteProcess(gcc + " -E -x c++ - -v < /dev/null 2>&1");
#endif
    auto begin = boost::algorithm::find_first(result, "#include <...> search starts here:");
    auto end = boost::algorithm::find_first(result, "End of search list.");
    if (!begin || !end)
        throw std::runtime_error("Could not find G++ header search paths in G++ output.");
    auto path_strings = std::string(begin.end(), end.begin());
    std::vector<std::string> paths;
    boost::algorithm::split(paths, path_strings, [](char c) { return c == '\n'; });
    std::vector<std::string> non_empty_paths;
    for (auto&& path : paths) {
        boost::algorithm::trim(path);
        if (!path.empty())
            non_empty_paths.push_back(path);
    }
    return non_empty_paths;
}