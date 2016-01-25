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
newoption {
    trigger = "zlib-path",
    value = "filepath",
    description = "The path of zlib on this machine."
}
newoption {
    trigger = "libarchive-path",
    value = "filepath",
    description = "The path of libarchive on this machine.",
}
newoption {
    trigger = "test",
    description = "If this option is passed, run the tests as part of building.",
}
if not os.is("Windows") then
    newoption {
        trigger = "TeamCity",
        value = "teamcity build number",
        description = "If this build is being built on TeamCity"
    }
end

local oldjoin = path.join
path.join = function(first, second, ...)
    if first and second then
        return path.join(oldjoin(first, second), ...)
    elseif first and not second then
        return first
    end
    error("Must provide at least one argument to path.join!\n" .. debug.traceback())
end

function AddClangDependencies(plat, conf)
    if _OPTIONS["llvm-path"] then
        local llvmconf = (os.is("windows") and conf) or _OPTIONS["llvm-conf"] or "Release+Asserts"
        local llvmbuild = (os.is("windows") and "build") or ""
        local llvmincludes = {
            "tools/clang/include", 
            path.join(llvmbuild, "tools/clang/include"), 
            "include", 
            "tools/clang/lib",
            path.join(llvmbuild, "include")
        }
        for k, v in pairs(llvmincludes) do
            includedirs({ path.join(_OPTIONS["llvm-path"], v) })
        end
        if os.is("windows") then
            if plat == 'x64' then
                llvmbuild = path.join(llvmbuild, 'x64')
            end
        end
        if os.is("windows") then
            libdirs({ path.join(_OPTIONS["llvm-path"], llvmbuild, llvmconf, "lib") })
        else
            libdirs({ path.join(_OPTIONS["llvm-path"], "Release+Asserts", "lib") })
        end
    else
        buildoptions({ "-I `llvm-config-3.6 --includedir`" })
        linkoptions({ "-L `llvm-config-3.6 --libdir`" })
        includedirs({ "Wide/clang-include" })
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
        "clangRewrite",
        "clangEdit",
        "clangAST",
        "clangLex",
        "clangBasic",
    }
     
    local libs = {
"LLVMLTO",
"LLVMObjCARCOpts",
"LLVMLinker",
"LLVMBitWriter",
"LLVMIRReader",
"LLVMAsmParser",
"LLVMR600CodeGen",
"LLVMipo",
"LLVMVectorize",
"LLVMR600AsmParser",
"LLVMR600Desc",
"LLVMR600Info",
"LLVMR600AsmPrinter",
"LLVMSystemZDisassembler",
"LLVMSystemZCodeGen",
"LLVMSystemZAsmParser",
"LLVMSystemZDesc",
"LLVMSystemZInfo",
"LLVMSystemZAsmPrinter",
"LLVMHexagonDisassembler",
"LLVMHexagonCodeGen",
"LLVMHexagonDesc",
"LLVMHexagonInfo",
"LLVMNVPTXCodeGen",
"LLVMNVPTXDesc",
"LLVMNVPTXInfo",
"LLVMNVPTXAsmPrinter",
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
"LLVMAArch64Disassembler",
"LLVMAArch64CodeGen",
"LLVMAArch64AsmParser",
"LLVMAArch64Desc",
"LLVMAArch64Info",
"LLVMAArch64AsmPrinter",
"LLVMAArch64Utils",
"LLVMARMDisassembler",
"LLVMARMCodeGen",
"LLVMARMAsmParser",
"LLVMARMDesc",
"LLVMARMInfo",
"LLVMARMAsmPrinter",
"LLVMPowerPCDisassembler",
"LLVMPowerPCCodeGen",
"LLVMPowerPCAsmParser",
"LLVMPowerPCDesc",
"LLVMPowerPCInfo",
"LLVMPowerPCAsmPrinter",
"LLVMSparcDisassembler",
"LLVMSparcCodeGen",
"LLVMSparcAsmParser",
"LLVMSparcDesc",
"LLVMSparcInfo",
"LLVMSparcAsmPrinter",
"LLVMTableGen",
"LLVMDebugInfo",
"LLVMOption",
"LLVMX86Disassembler",
"LLVMX86AsmParser",
"LLVMX86CodeGen",
"LLVMSelectionDAG",
"LLVMAsmPrinter",
"LLVMX86Desc",
"LLVMMCDisassembler",
"LLVMX86Info",
"LLVMX86AsmPrinter",
"LLVMX86Utils",
"LLVMMCJIT",
"LLVMLineEditor",
"LLVMInstrumentation",
"LLVMInterpreter",
"LLVMExecutionEngine",
"LLVMRuntimeDyld",
"LLVMCodeGen",
"LLVMScalarOpts",
"LLVMProfileData",
"LLVMObject",
"LLVMMCParser",
"LLVMBitReader",
"LLVMInstCombine",
"LLVMTransformUtils",
"LLVMipa",
"LLVMAnalysis",
"LLVMTarget",
"LLVMMC",
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
        links { "dl", "pthread", "ncurses" }
        return
    end
end
function AddBoostDependencies(plat, conf)
    if _OPTIONS["boost-path"] then
        local boostincludes = {
            ""
        }
        for k, v in pairs(boostincludes) do
            includedirs({ path.join(_OPTIONS["boost-path"], v) })
        end
        local boostlib = "stage"
        if os.is("windows") then
            if plat == "x64" then
                boostlib = path.join(boostlib, plat)
            end
        end
        libdirs({ path.join(_OPTIONS["boost-path"], boostlib, 'lib') })
    end
    if not os.is("windows") then
        links { "boost_program_options" }
    end
end
function AddLibarchiveDependency(plat, conf)
    if _OPTIONS["libarchive-path"] then
        includedirs({ path.join(_OPTIONS["libarchive-path"], 'libarchive' ) })
        libdirs({ path.join(_OPTIONS["libarchive-path"], "lib/libarchive", conf ) })
    end
    if os.is("windows") then 
        links { "archive_static" }
    else
        links { "archive" }
    end
end
function AddZlibDependency(plat, conf)
    if _OPTIONS["zlib-path"] then
        if plat == "x32" then plat = "x86" end
        libdirs({ path.join(_OPTIONS["zlib-path"], "zlib/contrib/vstudio/vc11", plat, "ZlibStat" .. conf ) })
        includedirs({ _OPTIONS["zlib-path"] })
    end 
    if os.is("windows") then
        links { "zlibstat" }
    else
        links { "z" }
    end
end
if os.is("windows") then
    if not _OPTIONS["boost-path"] then
        print("Error: boost-path was not provided.\n")
        return
    end
    if not _OPTIONS["libarchive-path"] then
        print("Error: libarchive-path was not provided.\n")
        return
    end
    if not _OPTIONS["zlib-path"] then
        print("Error: zlib-path was not provided.\n")
        return
    end
    if not _OPTIONS["llvm-path"] then
        print("Error: llvm-path was not provided.\n")
        return
    end
end

WideProjects = {
    CLI = { 
        action = function(plat, conf)
            links { "Util", "Lexer", "Parser", "Semantic" }
            kind ("ConsoleApp")
        end,
        configure = function(plat, conf)
            if os.is("windows") then
                postbuildcommands ({ "copy /Y \"$(TargetDir)$(TargetName).exe\" \"$(SolutionDir)Deployment/Wide.exe\"" })
            else
                postbuildcommands ({ "mkdir -p ../Deployment", "cp -f \"../Build/" .. plat .. "/" .. conf .. "/CLI\" \"../Deployment/Wide\"" })
            end
        end,
    },
    Util = {},
    Semantic = {
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
    CAPI = {
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
                postbuildcommands ({ "cp -r ../WideLibrary ../Deployment/" })
            end
        end
    },
    ParserTest = { 
        action = function() 
            kind("ConsoleApp")
            links { "Util", "Lexer", "Parser" }
        end,
        configure = function(plat, conf)
            if _OPTIONS["test"] then
                if os.is("windows") then
                    postbuildcommands ({ "$(TargetPath)" })
                else
                    postbuildcommands ({ "$@" })
                end
            end
        end,
    },
    LexerTest = { 
        action = function() 
            kind("ConsoleApp")
            links { "Util", "Lexer" }
        end,
        configure = function(plat, conf)
            if _OPTIONS["test"] then
                if os.is("windows") then
                    postbuildcommands ({ "$(TargetPath)" })
                else
                    postbuildcommands ({ "$@" })
                end
            end
        end,
    },
    IntegrationTests = {
        action = function(plat, conf)
            kind("ConsoleApp")
            links { "Util", "Lexer", "Parser", "Semantic" }
            files ({ "Wide/IntegrationTests/**.wide" })
        end,
        configure = function(plat, conf)
            if _OPTIONS["test"] then
                if os.is("windows") then
                    postbuildcommands ({ "$(TargetPath)" })
                else
                    postbuildcommands ({ "$@" })
                end
            end
        end,
    },
    SemanticTest = {
        action = function(plat, conf)
            kind("ConsoleApp")
            links { "Util", "Lexer", "Parser", "Semantic" }
        end,
        configure = function(plat, conf)
            if _OPTIONS["test"] then
                if os.is("windows") then
                    postbuildcommands ({ "$(TargetPath)" })
                else
                    postbuildcommands ({ "$@" })
                end
            end
        end   
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
    if _OPTIONS["TeamCity"] then
        defines { "TEAMCITY=" .. _OPTIONS["TeamCity"] }
    end
    buildoptions  {"-std=c++1y", "-D __STDC_CONSTANT_MACROS", "-D __STDC_LIMIT_MACROS", "-fPIC", "-Wno-deprecated" }
else
    defines { "_SCL_SECURE_NO_WARNINGS" }
    buildoptions  {"/wd4503" }
end
includedirs("./")
includedirs("./Wide/jsonpp/")
location("Wide")
for name, proj in pairs(WideProjects) do
    project(name)
    location(path.join("Wide", name))
    files( { path.join("Wide", name) .. "/**.cpp", path.join("Wide", name) .. "/**.h" })
    for k, plat in pairs(SupportedPlatforms) do
        for k, conf in pairs(SupportedConfigurations) do
            configuration { plat, conf }
            if proj.action then proj.action(plat, conf) end
            AddClangDependencies(plat, conf)
            AddBoostDependencies(plat, conf)
            AddLibarchiveDependency(plat, conf)
            AddZlibDependency(plat, conf)
            if proj.configure then proj.configure(plat, conf) end
            objdir(path.join("Wide/Obj", name))
            if conf == "Debug" then 
                flags("Symbols")
                defines { "NDEBUG" }
            else
                flags("Optimize")
                defines { "NDEBUG" }
            end
            targetdir(path.join("Wide/Build", plat, conf))
        end
    end
end
