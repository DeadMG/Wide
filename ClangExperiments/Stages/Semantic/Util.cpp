#include "Util.h"
#include "LvalueType.h"
#include <unordered_map>
#include <iostream>


#pragma warning(push, 0)

#include <CodeGen/CodeGenModule.h>
#include <clang/Frontend/CodeGenOptions.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/DeclGroup.h>
#include <clang/Lex/HeaderSearchOptions.h>

#pragma warning(pop)

namespace Wide {
    namespace Semantic {         
        clang::ExprValueKind GetKindOfType(Type* t) {
            if (dynamic_cast<Semantic::LvalueType*>(t))
                return clang::ExprValueKind::VK_LValue;
            else 
                return clang::ExprValueKind::VK_RValue;
        }
    }
    namespace ClangUtil {
        clang::LangOptions GetDefaultLangOpts() {
            clang::LangOptions langopts;
            langopts.WChar = true;
            langopts.CPlusPlus = true;
            langopts.CPlusPlus11 = true;
            langopts.Bool = true;
            langopts.GNUKeywords = true;
            langopts.Exceptions = true;
            langopts.CXXExceptions = true;
            return langopts;
        } 
        clang::CodeGenOptions GetDefaultCodeGenOpts() {
            clang::CodeGenOptions opts;
            opts.CXAAtExit = false;
            return opts;
        }
        clang::TargetInfo* CreateTargetInfoFromTriple(clang::DiagnosticsEngine& engine, std::string triple) {
            clang::TargetOptions& target = *new clang::TargetOptions();
            target.Triple = triple;
            auto targetinfo = clang::TargetInfo::CreateTargetInfo(engine, &target);
            targetinfo->setCXXABI(clang::TargetCXXABI::GenericItanium);
            return targetinfo;
        } 
        llvm::IntrusiveRefCntPtr<clang::HeaderSearchOptions> CreateHeaderSearchForMinGWIncludes(std::string MinGWInstallPath) {
            auto headeropts = llvm::IntrusiveRefCntPtr<clang::HeaderSearchOptions>(new clang::HeaderSearchOptions());
            headeropts->AddPath(MinGWInstallPath + "include\\c++\\4.6.3", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
            headeropts->AddPath(MinGWInstallPath + "include\\c++\\4.6.3\\i686-w64-mingw32", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
            headeropts->AddPath(MinGWInstallPath + "i686-w64-mingw32\\include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
            headeropts->AddPath("D:\\Backups\\Code\\LLVM\\include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
            headeropts->AddPath("D:\\Backups\\Code\\LLVM\\tools\\clang\\lib", clang::frontend::IncludeDirGroup::CXXSystem, false, false);
            headeropts->AddPath("D:\\Backups\\Code\\LLVM\\tools\\clang\\include", clang::frontend::IncludeDirGroup::CXXSystem, false, false);            
            return headeropts;
        }
        std::string GetDataLayoutForTriple(std::string triple) {
            std::unordered_map<std::string, std::string> values;
            values["i686-pc-mingw32"] = "e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-f80:128:128-v64:64:64-v128:128:128-a0:0:64-f80:32:32-n8:16:32-S32";
            if (values.find(triple) == values.end())
                throw std::runtime_error("Could not find DataLayout: unknown triple.");
            return values[triple];
        }
        std::size_t ClangTypeHasher::operator()(clang::QualType t) const {
            return llvm::DenseMapInfo<clang::QualType>::getHashValue(t);
        }       
    }
}



#pragma comment(lib, "clangAnalysis.lib")
#pragma comment(lib, "clangARCMigrate.lib")
#pragma comment(lib, "clangAST.lib")
#pragma comment(lib, "clangASTMatchers.lib")
#pragma comment(lib, "clangBasic.lib")

#pragma comment(lib, "clangCodeGen.lib")
#pragma comment(lib, "clangDriver.lib")
#pragma comment(lib, "clangEdit.lib")
#pragma comment(lib, "clangFormat.lib")
#pragma comment(lib, "clangFrontend.lib")

#pragma comment(lib, "clangFrontendTool.lib")
#pragma comment(lib, "clangLex.lib")
#pragma comment(lib, "clangParse.lib")
#pragma comment(lib, "clangRewriteCore.lib")
#pragma comment(lib, "clangRewriteFrontend.lib")

#pragma comment(lib, "clangSema.lib")
#pragma comment(lib, "clangSerialization.lib")
#pragma comment(lib, "clangStaticAnalyzerCheckers.lib")
#pragma comment(lib, "clangStaticAnalyzerCore.lib")
#pragma comment(lib, "clangStaticAnalyzerFrontend.lib")

#pragma comment(lib, "clangTooling.lib")

#pragma comment(lib, "LLVMAnalysis.lib")
#pragma comment(lib, "LLVMArchive.lib")
#pragma comment(lib, "LLVMAsmParser.lib")
#pragma comment(lib, "LLVMAsmPrinter.lib")
#pragma comment(lib, "LLVMBitReader.lib")

#pragma comment(lib, "LLVMBitWriter.lib")
#pragma comment(lib, "LLVMCodeGen.lib")
#pragma comment(lib, "LLVMCore.lib")
#pragma comment(lib, "LLVMDebugInfo.lib")
#pragma comment(lib, "LLVMExecutionEngine.lib")

#pragma comment(lib, "LLVMInstCombine.lib")
#pragma comment(lib, "LLVMInstrumentation.lib")
#pragma comment(lib, "LLVMInterpreter.lib")
#pragma comment(lib, "LLVMipa.lib")
#pragma comment(lib, "LLVMipo.lib")

#pragma comment(lib, "LLVMJIT.lib")
#pragma comment(lib, "LLVMLinker.lib")
#pragma comment(lib, "LLVMMC.lib")
#pragma comment(lib, "LLVMMCDisassembler.lib")
#pragma comment(lib, "LLVMMCJIT.lib")

#pragma comment(lib, "LLVMMCParser.lib")
#pragma comment(lib, "LLVMObject.lib")
#pragma comment(lib, "LLVMOption.lib")
#pragma comment(lib, "LLVMRuntimeDyld.lib")
#pragma comment(lib, "LLVMScalarOpts.lib")

#pragma comment(lib, "LLVMSelectionDAG.lib")
#pragma comment(lib, "LLVMSupport.lib")
#pragma comment(lib, "LLVMTableGen.lib")
#pragma comment(lib, "LLVMTarget.lib")
#pragma comment(lib, "LLVMTransformUtils.lib")

#pragma comment(lib, "LLVMVectorize.lib")
#pragma comment(lib, "LLVMX86AsmParser.lib")
#pragma comment(lib, "LLVMX86AsmPrinter.lib")
#pragma comment(lib, "LLVMX86CodeGen.lib")
#pragma comment(lib, "LLVMX86Desc.lib")

#pragma comment(lib, "LLVMX86Disassembler.lib")
#pragma comment(lib, "LLVMX86Info.lib")
#pragma comment(lib, "LLVMX86Utils.lib")