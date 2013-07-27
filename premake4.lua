newoption {
    trigger = "llvm-path",
    value = "filepath",
    description = "The path of LLVM on this machine."
}
newoption {
    trigger = "boost-path",
    value = "filepath",
    description = "The path of Boost on this machine."
}

function AddClangDependencies(conf)
    local llvmincludes = {
        "tools/clang/include", 
        "build/tools/clang/include", 
        "include", 
        "tools/clang/lib" 
    }
    for k, v in pairs(llvmincludes) do
        includedirs({ _OPTIONS["llvm-path"] .. v })
    end
    local llvmlibs = {
        "build/lib/" .. conf
    }
    for k, v in pairs(llvmlibs) do
        libdirs({ _OPTIONS["llvm-path"] .. v})
    end
    local libs = {
        "clangAnalysis",    
        "clangARCMigrate",
        "clangAST",
        "clangASTMatchers",
        "clangBasic",
        "clangCodeGen",
        "clangDriver",
        "clangEdit",
        "clangFormat",
        "clangFrontend",
        "clangFrontendTool",
        "clangLex",
        "clangParse",
        "clangRewriteCore",
        "clangRewriteFrontend",
        "clangSema",
        "clangSerialization",
        "clangStaticAnalyzerCheckers",
        "clangStaticAnalyzerCore",
        "clangStaticAnalyzerFrontend",
        "clangTooling",
        "LLVMAArch64AsmParser",
        "LLVMAArch64AsmPrinter",
        "LLVMAArch64CodeGen",
        "LLVMAArch64Desc",
        "LLVMAArch64Disassembler",
        "LLVMAArch64Info",
        "LLVMAArch64Utils",
        "LLVMAnalysis",
        "LLVMARMAsmParser",
        "LLVMARMAsmPrinter",
        "LLVMARMCodeGen",
        "LLVMARMDesc",
        "LLVMARMDisassembler",
        "LLVMARMInfo",
        "LLVMAsmParser",
        "LLVMAsmPrinter",
        "LLVMBitReader",
        "LLVMBitWriter",
        "LLVMCodeGen",
        "LLVMCore",
        "LLVMCppBackendCodeGen",
        "LLVMCppBackendInfo",
        "LLVMDebugInfo",
        "LLVMExecutionEngine",
        "LLVMHexagonAsmPrinter",
        "LLVMHexagonCodeGen",
        "LLVMHexagonDesc",
        "LLVMHexagonInfo",
        "LLVMInstCombine",
        "LLVMInstrumentation",
        "LLVMInterpreter",
        "LLVMipa",
        "LLVMipo",
        "LLVMIRReader",
        "LLVMJIT",
        "LLVMLinker",
        "LLVMMBlazeAsmParser",
        "LLVMMBlazeAsmPrinter",
        "LLVMMBlazeCodeGen",
        "LLVMMBlazeDesc",
        "LLVMMBlazeDisassembler",
        "LLVMMBlazeInfo",
        "LLVMMC",
        "LLVMMCDisassembler",
        "LLVMMCJIT",
        "LLVMMCParser",
        "LLVMMipsAsmParser",
        "LLVMMipsAsmPrinter",
        "LLVMMipsCodeGen",
        "LLVMMipsDesc",
        "LLVMMipsDisassembler",
        "LLVMMipsInfo",
        "LLVMMSP430AsmPrinter",
        "LLVMMSP430CodeGen",
        "LLVMMSP430Desc",
        "LLVMMSP430Info",
        "LLVMNVPTXAsmPrinter",
        "LLVMNVPTXCodeGen",
        "LLVMNVPTXDesc",
        "LLVMNVPTXInfo",
        "LLVMObjCARCOpts",
        "LLVMObject",
        "LLVMOption",
        "LLVMPowerPCAsmParser",
        "LLVMPowerPCAsmPrinter",
        "LLVMPowerPCCodeGen",
        "LLVMPowerPCDesc",
        "LLVMPowerPCInfo",
        "LLVMR600AsmPrinter",
        "LLVMR600CodeGen",
        "LLVMR600Desc",
        "LLVMR600Info",
        "LLVMRuntimeDyld",
        "LLVMScalarOpts",
        "LLVMSelectionDAG",
        "LLVMSparcCodeGen",
        "LLVMSparcDesc",
        "LLVMSparcInfo",
        "LLVMSupport",
        "LLVMSystemZAsmParser",
        "LLVMSystemZAsmPrinter",
        "LLVMSystemZCodeGen",
        "LLVMSystemZDesc",
        "LLVMSystemZDisassembler",
        "LLVMSystemZInfo",
        "LLVMTableGen",
        "LLVMTarget",
        "LLVMTransformUtils",
        "LLVMVectorize",
        "LLVMX86AsmParser",
        "LLVMX86AsmPrinter",
        "LLVMX86CodeGen",
        "LLVMX86Desc",
        "LLVMX86Disassembler",
        "LLVMX86Info",
        "LLVMX86Utils",
        "LLVMXCoreAsmPrinter",
        "LLVMXCoreCodeGen",
        "LLVMXCoreDesc",
        "LLVMXCoreDisassembler",
        "LLVMXCoreInfo"
    }
    for k, v in pairs(libs) do
        links { v }
    end
end
function AddBoostDependencies(conf)
    local boostincludes = {
        ""
    }
    for k, v in pairs(boostincludes) do
        includedirs({ _OPTIONS["boost-path"] .. v})
    end
    local boostlibs = {
        "stage/lib"
    }
    for k, v in pairs(boostlibs) do
        libdirs({ _OPTIONS["boost-path"] .. v})
    end
end

function CheckLLVM(proj)
    if not _OPTIONS["llvm-path"] then
        print("Error: llvm-path was not provided, skipping " .. proj .. ".\n")
        return false
    end
    return true
end
function CheckBoost(proj)
    if not _OPTIONS["boost-path"] then
        print("Error: boost-path was not provided, skipping " .. proj .. "\n")
        return false;
    end
    return true
end

local WideProjects = {
    { 
        name = "Driver", 
        dependencies = function(proj) 
            return CheckLLVM(proj) and CheckBoost(proj)
        end, 
        action = function()
            links { "Lexer", "Parser", "Semantic", "Codegen" }
            kind ("ConsoleApp")
        end,
        configure = function(plat, conf)
            AddClangDependencies(conf)
            AddBoostDependencies(conf)
            if os.is("windows") then
                postbuildcommands ({ "copy /Y \"$(TargetDir)$(TargetName).exe\" \"$(SolutionDir)Deployment/Wide.exe\"" })
            else
                postbuildcommands ({ "cp /Y \"../Build/" .. plat .. "/" .. conf .. "/Driver\" \"../Deployment/Wide\"" })
            end
        end
    },
    { 
        name = "Test", 
    },
    { 
        name = "Util", 
    },
    { 
        name = "Semantic", 
        dependencies = function(proj) 
            return CheckLLVM(proj) 
        end, 
        configure = function(plat, conf)
            AddClangDependencies(conf)
        end
    },
    { 
        name = "Parser", 
    },
    { 
        name = "Lexer", 
    },
    { 
        name = "Codegen", 
        dependencies = function(proj)
            return CheckLLVM(proj)
        end, 
        action = function()
            includedirs({ _OPTIONS["llvm-path"] .. "include" })
        end 
    },
    { 
        name = "CAPI", 
        action = function()
            kind "SharedLib"
            links { "Lexer" }
        end 
    },
}

local SupportedConfigurations = { "Debug", "Release" }
local SupportedPlatforms = { "x32", "x64" }
local sol = solution("Wide")
language("C++")
configurations(SupportedConfigurations)
platforms(SupportedPlatforms)
kind("StaticLib")
includedirs("./")
location("Wide")
for k, v in pairs(WideProjects) do
    if (not v.dependencies) or v.dependencies(v.name) then 
        project(v.name)
        location("Wide/"..v.name)
        if v.action then v.action() end
        files( { "Wide/" .. v.name .. "/**.cpp", "Wide/" .. v.name .. "/**.h" })
        for k, plat in pairs(SupportedPlatforms) do
            for k, conf in pairs(SupportedConfigurations) do
                configuration { plat, conf }
                if plat == "x32" then plat = "x86" end
                if v.configure then v.configure(plat, conf) end
                if conf == "Debug" then 
                    flags("Symbols")
                else
                    flags("Optimize")
                end
                targetdir("Wide/Build/" .. plat .. "/" .. conf .. "/")
            end
        end        
    end
end