#pragma once

#include <functional>
#include <string>
#include <iostream>

#pragma warning(push, 0)
#include <clang/Basic/FileSystemOptions.h>
#include <clang/Basic/DiagnosticIDs.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Lex/HeaderSearchOptions.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Frontend/FrontendOptions.h>
#include <clang/Frontend/CodeGenOptions.h>
#pragma warning(pop)

namespace Wide {    
    namespace Options {
        struct Clang {
            Clang()
                : OnDiagnostic([](std::string err) {
                      std::cout << err;
                  }) 
                , DiagnosticIDs(new clang::DiagnosticIDs())
                , DiagnosticOptions(new clang::DiagnosticOptions())           
                , HeaderSearchOptions(new clang::HeaderSearchOptions())
                , PreprocessorOptions(new clang::PreprocessorOptions())
            {
                // Default LanguageOptions:            
                LanguageOptions.WChar = true;
                LanguageOptions.CPlusPlus = true;
                LanguageOptions.CPlusPlus11 = true;
                LanguageOptions.Bool = true;
                LanguageOptions.GNUKeywords = true;
                LanguageOptions.Exceptions = true;
                LanguageOptions.CXXExceptions = true;
        
                // Default CodegenOptions:            
                CodegenOptions.CXAAtExit = false;
        
                FrontendOptions.OutputFile = "a.o";
            }
            std::function<void(std::string)> OnDiagnostic;
            clang::FileSystemOptions FileSearchOptions;
            llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> DiagnosticIDs;
            llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> DiagnosticOptions;
            clang::LangOptions LanguageOptions;
            clang::TargetOptions TargetOptions;
            llvm::IntrusiveRefCntPtr<clang::HeaderSearchOptions> HeaderSearchOptions;
            llvm::IntrusiveRefCntPtr<clang::PreprocessorOptions> PreprocessorOptions;
            clang::FrontendOptions FrontendOptions;
            clang::CodeGenOptions CodegenOptions;
        };
    }
}