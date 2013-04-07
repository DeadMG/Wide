// ClangExperiments.cpp : Defines the entry point for the console application.

#include "Stages\Library.h"
#include "Stages\ClangOptions.h"
#include "Stages\LLVMOptions.h"

int main()
{
    Wide::Options::Clang ClangOpts;
    Wide::Options::LLVM LLVMOpts;
    ClangOpts.TargetOptions.Triple = "i686-pc-mingw32";
    ClangOpts.FrontendOptions.OutputFile = "yay.o";
    
    const std::string MinGWInstallPath = "D:\\Backups\\unsorted\\i686-w64-mingw32-gcc-dw2-4.6.3-1-release-win32_rubenvb\\mingw32-dw2\\";    
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "include\\c++\\4.6.3", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "include\\c++\\4.6.3\\i686-w64-mingw32", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "i686-w64-mingw32\\include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);

    Wide::Library l(ClangOpts, LLVMOpts);
    l.AddWideFile("main.wide");

    l();
    
	return 0;
}
