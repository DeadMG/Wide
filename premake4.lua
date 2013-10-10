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
        "tools/clang/lib", 
        "build/include"
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

WideProjects = {
    CLI = { 
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
                postbuildcommands ({ "cp /Y \"../Build/" .. plat .. "/" .. conf .. "/CLI\" \"../Deployment/Wide\"" })
            end
        end,
    },
    Util = {},
    Semantic = { 
        dependencies = function(proj) 
            return CheckLLVM(proj) and CheckBoost(proj)
        end, 
        configure = function(plat, conf)
            AddClangDependencies(conf)
            AddBoostDependencies(conf)
        end
    },
    Parser = {},
    Lexer = {},
    Codegen = { 
        dependencies = function(proj)
            return CheckLLVM(proj)
        end, 
        configure = function(plat, conf)
            AddClangDependencies(conf)
        end
    },
    CAPI = {
        dependencies = function(proj)
            return CheckLLVM(proj) and CheckBoost(proj)
        end,
        configure = function(plat, conf)
            AddClangDependencies(conf)
            AddBoostDependencies(conf)
        end,            
        action = function()
            kind "SharedLib"
            links { "Lexer", "Parser", "Semantic" }
        end 
    },
    WideLibrary = {
        action = function()
            files ({ "Wide/WideLibrary/**.wide"})
            postbuildcommands ({ "(robocopy /mir \"Standard\" \"../Deployment/WideLibrary/Standard\") ^& IF %ERRORLEVEL% LEQ 1 exit 0" })
        end
    },
    LexerTest = { 
        action = function() 
            kind("ConsoleApp")
            links { "Lexer" }
        end,
        configure = function(plat, conf)
            if os.is("windows") then
                postbuildcommands ({ "$(TargetPath)" })
            else
                postbuildcommands ({ "LexerTest" })
            end
        end,
    },
    SemanticTest = {
        name = "SemanticTest",
        action = function()
            kind("ConsoleApp")
            links { "Lexer", "Parser", "Semantic" }
            files ({ "Wide/SemanticTest/**.wide" })
        end,
        dependencies = function(proj)
            return WideProjects.Semantic.dependencies(proj)
        end, 
        configure = function(plat, conf)
            WideProjects.Semantic.configure(plat, conf)
            if os.is("windows") then
                postbuildcommands ({ "$(TargetPath)" })
            else
                postbuildcommands ({ "SemanticTest" })
            end
        end,
    }        
}

local SupportedConfigurations = { "Debug", "Release" }
local SupportedPlatforms = { "x32", "x64" }
local sol = solution("Wide")
language("C++")
configurations(SupportedConfigurations)
platforms(SupportedPlatforms)
kind("StaticLib")
if not os.is("Windows") then
    buildoptions  {"-std=c++11", "-D __STDC_CONSTANT_MACROS"}
else
    defines { "_SCL_SECURE_NO_WARNINGS" }
end
includedirs("./")
location("Wide")
for name, proj in pairs(WideProjects) do
    if (not proj.dependencies) or proj.dependencies(name) then 
        project(name)
        location("Wide/"..name)
        if proj.action then proj.action() end
        files( { "Wide/" .. name .. "/**.cpp", "Wide/" .. name .. "/**.h" })
        for k, plat in pairs(SupportedPlatforms) do
            for k, conf in pairs(SupportedConfigurations) do
                configuration { plat, conf }
                if plat == "x32" then plat = "x86" end
                if proj.configure then proj.configure(plat, conf) end
                objdir("Wide/Obj/" .. plat .. "/" .. conf .. "/")
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