// ClangExperiments.cpp : Defines the entry point for the console application.

#include "Stages/Wide.h"

int main()
{
    Wide::Options::Clang ClangOpts;
    Wide::Options::LLVM LLVMOpts;

    ClangOpts.TargetOptions.Triple = "i686-pc-mingw32";
    ClangOpts.FrontendOptions.OutputFile = "yay.o";
    ClangOpts.LanguageOptions.CPlusPlus1y = true;
    
    const std::string MinGWInstallPath = "D:\\Backups\\unsorted\\MinGW\\mingw32-dw2\\";

    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "include\\c++\\4.6.3", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "include\\c++\\4.6.3\\i686-w64-mingw32", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
    ClangOpts.HeaderSearchOptions->AddPath(MinGWInstallPath + "i686-w64-mingw32\\include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);

    //LLVMOpts.Passes.push_back(Wide::Options::CreateDeadCodeElimination());

    std::vector<std::string> files;
    files.push_back("main.wide");
    Wide::Compile(ClangOpts, LLVMOpts, files);
    
	return 0;
}
