#pragma once

#include <string>
#include <vector>

#pragma warning(push, 0)
#include <llvm/Pass.h>
#pragma warning(pop)

namespace Wide {
    namespace Options {
        struct LLVM {
            std::vector<std::unique_ptr<llvm::Pass>> Passes;
            LLVM() {}
        };
        std::unique_ptr<llvm::Pass> CreateEdgeProfiler();
        std::unique_ptr<llvm::Pass> CreateStripDeadPrototypes();
        std::unique_ptr<llvm::Pass> CreatePathProfiler();
        std::unique_ptr<llvm::Pass> CreateBoundsChecking();
        std::unique_ptr<llvm::Pass> CreateStripSymbols();
        std::unique_ptr<llvm::Pass> CreateStripNonDebugSymbols();
        std::unique_ptr<llvm::Pass> CreateStripDebugDeclare();
        std::unique_ptr<llvm::Pass> CreateStripDeadDebugInfo();
        std::unique_ptr<llvm::Pass> CreateConstantMerge();
        std::unique_ptr<llvm::Pass> CreateGlobalOptimizer();
        std::unique_ptr<llvm::Pass> CreateGlobalDCE();
        std::unique_ptr<llvm::Pass> CreateFunctionInlining();
        std::unique_ptr<llvm::Pass> CreateAlwaysInliner();
        std::unique_ptr<llvm::Pass> CreatePruneEH();
        std::unique_ptr<llvm::Pass> CreateInternalize();
        std::unique_ptr<llvm::Pass> CreateDeadArgumentElimination();
        std::unique_ptr<llvm::Pass> CreateArgumentPromotion();
        std::unique_ptr<llvm::Pass> CreateIPConstantPropagation();
        std::unique_ptr<llvm::Pass> CreateIPSCCP();    
        std::unique_ptr<llvm::Pass> CreateLoopExtractor();
        std::unique_ptr<llvm::Pass> CreateSingleLoopExtractor();
        std::unique_ptr<llvm::Pass> CreateBlockExtractor();
        std::unique_ptr<llvm::Pass> CreateStripDeadPrototypes();
        std::unique_ptr<llvm::Pass> CreateFunctionAttrs();
        std::unique_ptr<llvm::Pass> CreateMergeFunctions();
        std::unique_ptr<llvm::Pass> CreatePartialInlining();
        std::unique_ptr<llvm::Pass> CreateMetaRenamer();
        std::unique_ptr<llvm::Pass> CreateBarrierNoop();
        std::unique_ptr<llvm::Pass> CreateObjCARCAPElim();
        std::unique_ptr<llvm::Pass> CreateObjCARCExpand();
        std::unique_ptr<llvm::Pass> CreateObjCARCContract();
        std::unique_ptr<llvm::Pass> CreateObjCARCOpt();
        std::unique_ptr<llvm::Pass> CreateConstantPropagation();
        std::unique_ptr<llvm::Pass> CreateSCCP();
        std::unique_ptr<llvm::Pass> CreateDeadInstElimination();
        std::unique_ptr<llvm::Pass> CreateDeadCodeElimination();
        std::unique_ptr<llvm::Pass> CreateDeadStoreElimination();
        std::unique_ptr<llvm::Pass> CreateAggressiveDCE();
        std::unique_ptr<llvm::Pass> CreateScalarReplacementOfAggregates();
        std::unique_ptr<llvm::Pass> CreateScalarReplAggregates();
        std::unique_ptr<llvm::Pass> CreateIndVarSimplify();
        std::unique_ptr<llvm::Pass> CreateInstructionCombining();
        std::unique_ptr<llvm::Pass> CreateLICM();
        std::unique_ptr<llvm::Pass> CreateLoopStrengthReduce();
        std::unique_ptr<llvm::Pass> CreateGlobalMerge();
        std::unique_ptr<llvm::Pass> CreateLoopUnswitch();
        std::unique_ptr<llvm::Pass> CreateLoopInstSimplify();
        std::unique_ptr<llvm::Pass> CreateLoopUnroll();
        std::unique_ptr<llvm::Pass> CreateLoopRotate();
        std::unique_ptr<llvm::Pass> CreateLoopIdiom();
        std::unique_ptr<llvm::Pass> CreatePromoteMemoryToRegister();
        std::unique_ptr<llvm::Pass> CreateDemoteRegisterToMemory();
        std::unique_ptr<llvm::Pass> CreateReassociate();
        std::unique_ptr<llvm::Pass> CreateJumpThreading();
        std::unique_ptr<llvm::Pass> CreateCFGSimplification();
        std::unique_ptr<llvm::Pass> CreateBreakCriticalEdges();
        std::unique_ptr<llvm::Pass> CreateLoopSimplify();
        std::unique_ptr<llvm::Pass> CreateTailCallElimination();
        std::unique_ptr<llvm::Pass> CreateLowerSwitch();
        std::unique_ptr<llvm::Pass> CreateLowerInvoke();
        std::unique_ptr<llvm::Pass> CreateBlockPlacement();
        std::unique_ptr<llvm::Pass> CreateLCSSA();
        std::unique_ptr<llvm::Pass> CreateEarlyCSE();
        std::unique_ptr<llvm::Pass> CreateGVN();
        std::unique_ptr<llvm::Pass> CreateMemCpyOpt();
        std::unique_ptr<llvm::Pass> CreateLoopDeletion();
        std::unique_ptr<llvm::Pass> CreateSimplifyLibCalls();
        std::unique_ptr<llvm::Pass> CreateCodeGenPrepare();
        std::unique_ptr<llvm::Pass> CreateInstructionNamer();
        std::unique_ptr<llvm::Pass> CreateSinking();
        std::unique_ptr<llvm::Pass> CreateLowerAtomic();
        std::unique_ptr<llvm::Pass> CreateCorrelatedValuePropagation();
        std::unique_ptr<llvm::Pass> CreateInstructionSimplifier();
        std::unique_ptr<llvm::Pass> CreateLowerExpectIntrinsic();
    }
}