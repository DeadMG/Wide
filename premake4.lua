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
if not os.is("Windows") then
    newoption {
        trigger = "llvm-conf",
        value = "configuration",
        description = "The configuration used to build LLVM."
    }
    newoption {
        trigger = "boost-lib",
        value = "library name",
        description = "The name of the Boost library."
    }
    newoption {
        trigger = "llvm-from-source",
        value = "boolean",
        description = "If LLVM was built from source"
    }
end


function AddClangDependencies(conf)
    local llvmconf = (os.is("windows") and conf) or _OPTIONS["llvm-conf"] or conf
    local llvmincludes = {
        "tools/clang/include", 
        "build/tools/clang/include", 
        "include", 
        "build/include"
    }
    if not _OPTIONS["llvm-from-package"] then
        for k, v in pairs(llvmincludes) do
            includedirs({ _OPTIONS["llvm-path"] .. v })
        end
        if os.is("windows") then
            libdirs({ _OPTIONS["llvm-path"] .. "build/lib/" .. llvmconf })
        else
            libdirs({ _OPTIONS["llvm-path"] .. "build/" .. llvmconf .. "/lib" })
        end
    else
        includedirs({ _OPTIONS["llvm-path"] })
    end
    local clanglibs = { 
        "clangFrontend",
        "clangSerialization",
        "clangDriver",
        "clangTooling",
        "clangCodeGen",
        "clangParse",
        "clangSema",
        "clangAnalysis",
        "clangRewriteFrontend",
        "clangRewriteCore",
        "clangEdit",
        "clangAST",
        "clangLex",
        "clangBasic",
    }
    if not os.is("windows") then
        for k, v in pairs(clanglibs) do
            linkoptions { "-l" .. v }
        end
        linkoptions { "`../../" .. _OPTIONS["llvm-path"] .. "build/" .. llvmconf .. "/bin/llvm-config --libs all`" }
        linkoptions { "-ldl", "-lpthread" }
        return
    end
    local libs = {
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
        "LLVMXCoreInfo",
        "LLVMSystemZDisassembler",
        "LLVMR600AsmPrinter",
        "LLVMR600CodeGen",
        "LLVMR600Desc",
        "LLVMR600Info"
    }
    for k, v in pairs(libs) do
        links { v }
    end
    for k, v in pairs(clanglibs) do
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
    if not os.is("windows") then
        links { "boost_program_options" }
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
            links { "Codegen", "Util", "Lexer", "Parser", "Semantic" }
            kind ("ConsoleApp")
        end,
        configure = function(plat, conf)
            AddClangDependencies(conf)
            AddBoostDependencies(conf)
            if os.is("windows") then
                postbuildcommands ({ "copy /Y \"$(TargetDir)$(TargetName).exe\" \"$(SolutionDir)Deployment/Wide.exe\"" })
            else
                postbuildcommands ({ "mkdir -p ../Deployment" ,  "cp -f \"../Build/" .. plat .. "/" .. conf .. "/CLI\" \"../Deployment/CLI\"" })
            end
        end,
    },
    Util = {
        dependencies = function(proj) 
            return CheckLLVM(proj) and CheckBoost(proj)
        end, 
        configure = function(plat, conf)
            AddClangDependencies(conf)
            AddBoostDependencies(conf)
        end
    },
    Semantic = { 
        dependencies = function(proj) 
            return CheckLLVM(proj) and CheckBoost(proj)
        end, 
        configure = function(plat, conf)
            AddClangDependencies(conf)
            AddBoostDependencies(conf)
        end,
        action = function()
            links { "Util" }
        end
    },
    Parser = {
        action = function()
            links { "Util" }
        end
    },
    Lexer = {
        action = function()
            links { "Util" }
        end
    },
    Codegen = { 
        dependencies = function(proj)
            return CheckLLVM(proj)
        end, 
        configure = function(plat, conf)
            AddClangDependencies(conf)
        end,
        action = function()
            links { "Util" }
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
            links { "Util", "Lexer", "Parser", "Semantic" }
        end 
    },
    WideLibrary = {
        action = function()
            files ({ "Wide/WideLibrary/**.wide"})
            if os.is("windows") then
                postbuildcommands ({ "(robocopy /mir \"Standard\" \"../Deployment/WideLibrary/Standard\") ^& IF %ERRORLEVEL% LEQ 1 exit 0" })
            else
                postbuildcommands ({ "cp -r ../WideLibrary ../Deployment/WideLibrary" })
            end
        end
    },
    LexerTest = { 
        action = function() 
            kind("ConsoleApp")
            links { "Util", "Lexer" }
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
            links { "Util", "Lexer", "Parser", "Semantic", "Codegen" }
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
                postbuildcommands ({ "../Build/x64/Debug/SemanticTest" })
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
    buildoptions  {"-std=c++11", "-D __STDC_CONSTANT_MACROS", "-D __STDC_LIMIT_MACROS", "-fPIC" }
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
