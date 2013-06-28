#include "Util.h"
#include "Reference.h"
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
        clang::TargetInfo* CreateTargetInfoFromTriple(clang::DiagnosticsEngine& engine, std::string triple) {
            clang::TargetOptions& target = *new clang::TargetOptions();
            target.Triple = triple;
            auto targetinfo = clang::TargetInfo::CreateTargetInfo(engine, &target);
            targetinfo->setCXXABI(clang::TargetCXXABI::GenericItanium);
            return targetinfo;
        } 
        std::size_t ClangTypeHasher::operator()(clang::QualType t) const {
            return llvm::DenseMapInfo<clang::QualType>::getHashValue(t);
        }       
    }
}

#ifdef _MSC_VER
#pragma comment( lib, "clangAnalysis.lib" )
#pragma comment( lib, "clangARCMigrate.lib" )
#pragma comment( lib, "clangAST.lib" )
#pragma comment( lib, "clangASTMatchers.lib" )
#pragma comment( lib, "clangBasic.lib" )
#pragma comment( lib, "clangCodeGen.lib" )
#pragma comment( lib, "clangDriver.lib" )
#pragma comment( lib, "clangDynamicASTMatchers.lib" )
#pragma comment( lib, "clangEdit.lib" )
#pragma comment( lib, "clangFormat.lib" )
#pragma comment( lib, "clangFrontend.lib" )
#pragma comment( lib, "clangFrontendTool.lib" )
#pragma comment( lib, "clangLex.lib" )
#pragma comment( lib, "clangParse.lib" )
#pragma comment( lib, "clangRewriteCore.lib" )
#pragma comment( lib, "clangRewriteFrontend.lib" )
#pragma comment( lib, "clangSema.lib" )
#pragma comment( lib, "clangSerialization.lib" )
#pragma comment( lib, "clangStaticAnalyzerCheckers.lib" )
#pragma comment( lib, "clangStaticAnalyzerCore.lib" )
#pragma comment( lib, "clangStaticAnalyzerFrontend.lib" )
#pragma comment( lib, "clangTooling.lib" )
#pragma comment( lib, "LLVMAArch64AsmParser.lib" )
#pragma comment( lib, "LLVMAArch64AsmPrinter.lib" )
#pragma comment( lib, "LLVMAArch64CodeGen.lib" )
#pragma comment( lib, "LLVMAArch64Desc.lib" )
#pragma comment( lib, "LLVMAArch64Disassembler.lib" )
#pragma comment( lib, "LLVMAArch64Info.lib" )
#pragma comment( lib, "LLVMAArch64Utils.lib" )
#pragma comment( lib, "LLVMAnalysis.lib" )
#pragma comment( lib, "LLVMARMAsmParser.lib" )
#pragma comment( lib, "LLVMARMAsmPrinter.lib" )
#pragma comment( lib, "LLVMARMCodeGen.lib" )
#pragma comment( lib, "LLVMARMDesc.lib" )
#pragma comment( lib, "LLVMARMDisassembler.lib" )
#pragma comment( lib, "LLVMARMInfo.lib" )
#pragma comment( lib, "LLVMAsmParser.lib" )
#pragma comment( lib, "LLVMAsmPrinter.lib" )
#pragma comment( lib, "LLVMBitReader.lib" )
#pragma comment( lib, "LLVMBitWriter.lib" )
#pragma comment( lib, "LLVMCodeGen.lib" )
#pragma comment( lib, "LLVMCore.lib" )
#pragma comment( lib, "LLVMCppBackendCodeGen.lib" )
#pragma comment( lib, "LLVMCppBackendInfo.lib" )
#pragma comment( lib, "LLVMDebugInfo.lib" )
#pragma comment( lib, "LLVMExecutionEngine.lib" )
#pragma comment( lib, "LLVMHexagonAsmPrinter.lib" )
#pragma comment( lib, "LLVMHexagonCodeGen.lib" )
#pragma comment( lib, "LLVMHexagonDesc.lib" )
#pragma comment( lib, "LLVMHexagonInfo.lib" )
#pragma comment( lib, "LLVMInstCombine.lib" )
#pragma comment( lib, "LLVMInstrumentation.lib" )
#pragma comment( lib, "LLVMInterpreter.lib" )
#pragma comment( lib, "LLVMipa.lib" )
#pragma comment( lib, "LLVMipo.lib" )
#pragma comment( lib, "LLVMIRReader.lib" )
#pragma comment( lib, "LLVMJIT.lib" )
#pragma comment( lib, "LLVMLinker.lib" )
#pragma comment( lib, "LLVMMBlazeAsmParser.lib" )
#pragma comment( lib, "LLVMMBlazeAsmPrinter.lib" )
#pragma comment( lib, "LLVMMBlazeCodeGen.lib" )
#pragma comment( lib, "LLVMMBlazeDesc.lib" )
#pragma comment( lib, "LLVMMBlazeDisassembler.lib" )
#pragma comment( lib, "LLVMMBlazeInfo.lib" )
#pragma comment( lib, "LLVMMC.lib" )
#pragma comment( lib, "LLVMMCDisassembler.lib" )
#pragma comment( lib, "LLVMMCJIT.lib" )
#pragma comment( lib, "LLVMMCParser.lib" )
#pragma comment( lib, "LLVMMipsAsmParser.lib" )
#pragma comment( lib, "LLVMMipsAsmPrinter.lib" )
#pragma comment( lib, "LLVMMipsCodeGen.lib" )
#pragma comment( lib, "LLVMMipsDesc.lib" )
#pragma comment( lib, "LLVMMipsDisassembler.lib" )
#pragma comment( lib, "LLVMMipsInfo.lib" )
#pragma comment( lib, "LLVMMSP430AsmPrinter.lib" )
#pragma comment( lib, "LLVMMSP430CodeGen.lib" )
#pragma comment( lib, "LLVMMSP430Desc.lib" )
#pragma comment( lib, "LLVMMSP430Info.lib" )
#pragma comment( lib, "LLVMNVPTXAsmPrinter.lib" )
#pragma comment( lib, "LLVMNVPTXCodeGen.lib" )
#pragma comment( lib, "LLVMNVPTXDesc.lib" )
#pragma comment( lib, "LLVMNVPTXInfo.lib" )
#pragma comment( lib, "LLVMObjCARCOpts.lib" )
#pragma comment( lib, "LLVMObject.lib" )
#pragma comment( lib, "LLVMOption.lib" )
#pragma comment( lib, "LLVMPowerPCAsmParser.lib" )
#pragma comment( lib, "LLVMPowerPCAsmPrinter.lib" )
#pragma comment( lib, "LLVMPowerPCCodeGen.lib" )
#pragma comment( lib, "LLVMPowerPCDesc.lib" )
#pragma comment( lib, "LLVMPowerPCInfo.lib" )
#pragma comment( lib, "LLVMR600AsmPrinter.lib" )
#pragma comment( lib, "LLVMR600CodeGen.lib" )
#pragma comment( lib, "LLVMR600Desc.lib" )
#pragma comment( lib, "LLVMR600Info.lib" )
#pragma comment( lib, "LLVMRuntimeDyld.lib" )
#pragma comment( lib, "LLVMScalarOpts.lib" )
#pragma comment( lib, "LLVMSelectionDAG.lib" )
#pragma comment( lib, "LLVMSparcCodeGen.lib" )
#pragma comment( lib, "LLVMSparcDesc.lib" )
#pragma comment( lib, "LLVMSparcInfo.lib" )
#pragma comment( lib, "LLVMSupport.lib" )
#pragma comment( lib, "LLVMSystemZAsmParser.lib" )
#pragma comment( lib, "LLVMSystemZAsmPrinter.lib" )
#pragma comment( lib, "LLVMSystemZCodeGen.lib" )
#pragma comment( lib, "LLVMSystemZDesc.lib" )
#pragma comment( lib, "LLVMSystemZDisassembler.lib" )
#pragma comment( lib, "LLVMSystemZInfo.lib" )
#pragma comment( lib, "LLVMTableGen.lib" )
#pragma comment( lib, "LLVMTarget.lib" )
#pragma comment( lib, "LLVMTransformUtils.lib" )
#pragma comment( lib, "LLVMVectorize.lib" )
#pragma comment( lib, "LLVMX86AsmParser.lib" )
#pragma comment( lib, "LLVMX86AsmPrinter.lib" )
#pragma comment( lib, "LLVMX86CodeGen.lib" )
#pragma comment( lib, "LLVMX86Desc.lib" )
#pragma comment( lib, "LLVMX86Disassembler.lib" )
#pragma comment( lib, "LLVMX86Info.lib" )
#pragma comment( lib, "LLVMX86Utils.lib" )
#pragma comment( lib, "LLVMXCoreAsmPrinter.lib" )
#pragma comment( lib, "LLVMXCoreCodeGen.lib" )
#pragma comment( lib, "LLVMXCoreDesc.lib" )
#pragma comment( lib, "LLVMXCoreDisassembler.lib" )
#pragma comment( lib, "LLVMXCoreInfo.lib" )
#endif