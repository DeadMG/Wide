#include <Wide/Util/Driver/StdlibDirectorySearch.h>
#include <set>

#pragma warning(push, 0)
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/ADT/Triple.h>
#pragma warning(pop)

std::unordered_set<std::string> Wide::Driver::SearchStdlibDirectory(std::string path, std::string triple) {
    auto trip = llvm::Triple(triple);
    if (!trip.isOSWindows() && !trip.isOSLinux() && !trip.isMacOSX() && !trip.getEnvironment()) {
        throw std::runtime_error("Error: Wide only supports targetting Windows, Mac, and Linux right now.\n");
    }
    std::string system =
        trip.isOSWindows() ? "Windows" :
        trip.isOSLinux() ? "Linux" :
        "Mac";
    std::unordered_set<std::string> ret;
    auto end = llvm::sys::fs::directory_iterator();
    std::error_code fuck_error_codes;
    bool out = true;
    auto begin = llvm::sys::fs::directory_iterator(path, fuck_error_codes);
    std::set<std::string> entries;
    while (!fuck_error_codes && begin != end) {
        entries.insert(begin->path());
        begin.increment(fuck_error_codes);
    }
    if (llvm::sys::path::filename(path) == "System") {
        llvm::SmallVector<char, 1> fuck_out_parameters;
        llvm::sys::path::append(fuck_out_parameters, path, system);
        std::string systempath(fuck_out_parameters.begin(), fuck_out_parameters.end());
        return SearchStdlibDirectory(systempath, triple);
    }
    for (auto file : entries) {
        bool isfile = false;
        llvm::sys::fs::is_regular_file(file, isfile);
        if (isfile) {
            if (llvm::sys::path::extension(file) == ".wide")
                ret.insert(file);
        }
        llvm::sys::fs::is_directory(file, isfile);
        if (isfile) {
            auto more = SearchStdlibDirectory(file, triple);
            ret.insert(more.begin(), more.end());
        }
    }
    return ret;
}