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
        trigger = "llvm-from-package",
        value = "boolean",
        description = "If LLVM was built from source"
    }
end

local oldjoin = path.join
path.join = function(first, second, ...)
    if first and second then
        return path.join(oldjoin(first, second), ...)
    elseif first and not second then
        return first
    end
    error("Must provide at least one argument to path.join!")
end

function AddClangDependencies(conf)
    local llvmconf = (os.is("windows") and conf) or _OPTIONS["llvm-conf"] or conf
    local llvmincludes = {
        "tools/clang/include", 
        "build/tools/clang/include", 
        "include", 
        "tools/clang/lib",
        "build/include"
    }
    if not _OPTIONS["llvm-from-package"] then
        for k, v in pairs(llvmincludes) do
            includedirs({ path.join(_OPTIONS["llvm-path"], v) })
        end
        if os.is("windows") then
            libdirs({ path.join(_OPTIONS["llvm-path"], "build/lib", llvmconf) })
        else
            libdirs({ path.join(_OPTIONS["llvm-path"], "build", llvmconf, "lib") })
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
    local libs = {
        "LLVMInstrumentation",
        "LLVMArchive",
        "LLVMLinker",
        "LLVMIRReader",
        "LLVMBitReader",
        "LLVMAsmParser",
        "LLVMDebugInfo",
        "LLVMOption",
        "LLVMipo",
        "LLVMVectorize",
        "LLVMBitWriter",
        "LLVMTableGen",
        "LLVMSystemZCodeGen",
        "LLVMSystemZAsmParser",
        "LLVMSystemZDesc",
        "LLVMSystemZInfo",
        "LLVMSystemZAsmPrinter",
        "LLVMHexagonCodeGen",
        "LLVMHexagonAsmPrinter",
        "LLVMHexagonDesc",
        "LLVMHexagonInfo",
        "LLVMNVPTXCodeGen",
        "LLVMNVPTXDesc",
        "LLVMNVPTXInfo",
        "LLVMNVPTXAsmPrinter",
        "LLVMMBlazeDisassembler",
        "LLVMMBlazeCodeGen",
        "LLVMMBlazeDesc",
        "LLVMMBlazeAsmPrinter",
        "LLVMMBlazeAsmParser",
        "LLVMMBlazeInfo",
        "LLVMCppBackendCodeGen",
        "LLVMCppBackendInfo",
        "LLVMMSP430CodeGen",
        "LLVMMSP430Desc",
        "LLVMMSP430Info",
        "LLVMMSP430AsmPrinter",
        "LLVMXCoreDisassembler",
        "LLVMXCoreCodeGen",
        "LLVMXCoreDesc",
        "LLVMXCoreInfo",
        "LLVMXCoreAsmPrinter",
        "LLVMMipsDisassembler",
        "LLVMMipsCodeGen",
        "LLVMMipsAsmParser",
        "LLVMMipsDesc",
        "LLVMMipsInfo",
        "LLVMMipsAsmPrinter",
        "LLVMARMDisassembler",
        "LLVMARMCodeGen",
        "LLVMARMAsmParser",
        "LLVMARMDesc",
        "LLVMARMInfo",
        "LLVMARMAsmPrinter",
        "LLVMAArch64Disassembler",
        "LLVMAArch64CodeGen",
        "LLVMAArch64AsmParser",
        "LLVMAArch64Desc",
        "LLVMAArch64Info",
        "LLVMAArch64AsmPrinter",
        "LLVMAArch64Utils",
        "LLVMPowerPCCodeGen",
        "LLVMPowerPCDesc",
        "LLVMPowerPCAsmPrinter",
        "LLVMPowerPCAsmParser",
        "LLVMPowerPCInfo",
        "LLVMSparcCodeGen",
        "LLVMSparcDesc",
        "LLVMSparcInfo",
        "LLVMX86Disassembler",
        "LLVMX86AsmParser",
        "LLVMX86CodeGen",
        "LLVMSelectionDAG",
        "LLVMAsmPrinter",
        "LLVMX86Desc",
        "LLVMX86Info",
        "LLVMX86AsmPrinter",
        "LLVMX86Utils",
        "LLVMMCDisassembler",
        "LLVMMCParser",
        "LLVMInterpreter",
        "LLVMMCJIT",
        "LLVMJIT",
        "LLVMCodeGen",
        "LLVMObjCARCOpts",
        "LLVMScalarOpts",
        "LLVMInstCombine",
        "LLVMTransformUtils",
        "LLVMipa",
        "LLVMAnalysis",
        "LLVMRuntimeDyld",
        "LLVMExecutionEngine",
        "LLVMTarget",
        "LLVMMC",
        "LLVMObject",
        "LLVMCore",
        "LLVMSupport"
    }
    for k, v in pairs(clanglibs) do
        links { v }
    end
    for k, v in pairs(libs) do
        links { v }
    end
    if not os.is("windows") then
        links { "dl", "pthread" }
        return
    end
end
function AddBoostDependencies(conf)
    local boostincludes = {
        ""
    }
    for k, v in pairs(boostincludes) do
        includedirs({ path.join(_OPTIONS["boost-path"], v) })
    end
    local boostlibs = {
        "stage/lib"
    }
    for k, v in pairs(boostlibs) do
        libdirs({ path.join(_OPTIONS["boost-path"], v) })
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
    ParserTest = { 
        action = function() 
            kind("ConsoleApp")
            links { "Util", "Lexer", "Parser" }
        end,
        configure = function(plat, conf)
            if os.is("windows") then
                postbuildcommands ({ "$(TargetPath)" })
            else
                postbuildcommands ({ "ParserTest" })
            end
        end,
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
        location(path.join("Wide", name))
        if proj.action then proj.action() end
        files( { path.join("Wide", name) .. "/**.cpp", path.join("Wide", name) .. "/**.h" })
        for k, plat in pairs(SupportedPlatforms) do
            for k, conf in pairs(SupportedConfigurations) do
                configuration { plat, conf }
                if plat == "x32" then plat = "x86" end
                if proj.configure then proj.configure(plat, conf) end
                objdir(path.join("Wide/Obj", plat, conf))
                if conf == "Debug" then 
                    flags("Symbols")
                else
                    flags("Optimize")
                end
                targetdir(path.join("Wide/Build", plat, conf))
            end
        end        
    end
end
