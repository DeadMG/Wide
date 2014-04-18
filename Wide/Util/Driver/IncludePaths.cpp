#include <Wide/Util/Driver/IncludePaths.h>
#include <Wide/Semantic/ClangOptions.h>
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
void Wide::Driver::AddLinuxIncludePaths(Options::Clang& ClangOpts) {
    auto end = llvm::sys::fs::directory_iterator();
    llvm::error_code fuck_error_codes;
    bool out = true;
    auto path = "/usr/include/c++";
    fuck_error_codes = llvm::sys::fs::is_directory(path, out);
    if (!out || fuck_error_codes) {
        throw std::runtime_error("Could not find the libstdc++ install path.");
    }
    auto begin = llvm::sys::fs::directory_iterator(path, fuck_error_codes);
    std::set<std::string> entries;
    while (!fuck_error_codes && begin != end) {
        entries.insert(begin->path());
        begin.increment(fuck_error_codes);
    }
    if (entries.size() == 1)
        return AddLinuxIncludePaths(ClangOpts, *entries.begin());
    std::set<std::tuple<int, int, int>> versions;
    for (auto entry : entries) {
        // Expecting entry in the form X.Y.Z
        if (entry.size() != 5)
            continue;
        auto value_from_character = [](char c) -> int {
            switch (c) {
            case '0': return 0;
            case '1': return 1;
            case '2': return 2;
            case '3': return 3;
            case '4': return 4;
            case '5': return 5;
            case '6': return 6;
            case '7': return 7;
            case '8': return 8;
            case '9': return 9;
            };
        };
        versions.insert(std::make_tuple(value_from_character(entry[0]), value_from_character(entry[2]), value_from_character(entry[4])));
    }
    if (versions.size() == 0)
        throw std::runtime_error("Could not find the libstdc++ install path.");
    auto latest = *--versions.end();
    return AddLinuxIncludePaths(ClangOpts, std::get<0>(latest), std::get<1>(latest), std::get<2>(latest));
}
void Wide::Driver::AddMinGWIncludePaths(Options::Clang& ClangOpts, std::string MinGWInstallPath) {
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\include\\c++\\4.6.3", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\include\\c++\\4.6.3\\i686-w64-mingw32", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "mingw32-dw2\\i686-w64-mingw32\\include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
}