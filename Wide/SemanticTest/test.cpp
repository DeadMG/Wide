#include <Wide/Util/Driver/Compile.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Util/DebugBreak.h>
#include <Wide/Util/Driver/IncludePaths.h>
#include <Wide/SemanticTest/test.h>
#include <Wide/Semantic/Expression.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Function.h>
#include <Wide/Semantic/FunctionType.h>
#include <Wide/Semantic/TupleType.h>
#include <Wide/Semantic/StringType.h>
#include <Wide/Util/Codegen/CreateModule.h>
#include <Wide/Parser/AST.h>
#include <Wide/Util/Driver/StdlibDirectorySearch.h>
#include <Wide/Util/Driver/Process.h>

#pragma warning(push, 0)
#include <llvm/ExecutionEngine/GenericValue.h>
// Gotta include the header or creating JIT won't work... fucking LLVM.
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/IR/Verifier.h>
#pragma warning(pop)

void Wide::Driver::TestDirectory(std::string path, std::string mode, std::string program, bool debugbreak, std::unordered_map<std::string, std::function<bool()>>& failedset) {
    unsigned tests_failed = 0;
    unsigned tests_succeeded = 0;
    auto run_test_process = [mode, program, debugbreak](std::string file) {
        auto modearg = "--mode=" + mode;
        std::string arguments = "--input=" + file;
        const char* args[] = { program.c_str(), arguments.c_str(), modearg.c_str(), nullptr };
        std::string err = "";
		Wide::Util::optional<unsigned> timeout;
		if (debugbreak) timeout = 1000000;
		return Wide::Driver::StartAndWaitForProcess(
			program,
			{ "--input=" + file, "--mode=" + mode },
			timeout
	    );
    };

    auto end = llvm::sys::fs::directory_iterator();
    std::error_code fuck_error_codes;
    bool out = true;
    fuck_error_codes = llvm::sys::fs::is_directory(path, out);
    if (!out || fuck_error_codes) {
        std::cout << "Skipping " << path << " as a directory by this name did not exist.\n";
        return;
    }
    auto begin = llvm::sys::fs::directory_iterator(path, fuck_error_codes);
    std::set<std::string> entries;
    while (!fuck_error_codes && begin != end) {
        entries.insert(begin->path());
        begin.increment(fuck_error_codes);
    }
    for (auto file : entries) {
        bool isfile = false;
        llvm::sys::fs::is_regular_file(file, isfile);
        if (isfile) {
            if (llvm::sys::path::extension(file) == ".wide")
                failedset[file] = [run_test_process, file] { return run_test_process(file); };
        }
        llvm::sys::fs::is_directory(file, isfile);
        if (isfile)
            TestDirectory(file, mode, program, debugbreak, failedset);        
    }
}

template<typename F> auto GenerateCode(llvm::Module* mod, F f) -> decltype(f(nullptr)) {
    llvm::EngineBuilder b(mod);
    b.setAllocateGVsWithCode(false);
    b.setEngineKind(llvm::EngineKind::JIT);
    b.setUseMCJIT(true);
    std::string errstring;
    b.setErrorStr(&errstring);
    auto ee = b.create();
    if (!ee) throw std::runtime_error("Failed to create ExecutionEngine!");
    ee->runStaticConstructorsDestructors(false);
    struct help {
        llvm::ExecutionEngine* ee;
        llvm::Module* mod;
        ~help() {
            ee->runStaticConstructorsDestructors(true);
            ee->removeModule(mod);
        }
    };
    help h{ ee, mod };
    return f(ee);
}

void Wide::Driver::Jit(Wide::Options::Clang& copts, std::string file) {
#ifdef _MSC_VER
    const std::string MinGWInstallPath = "../Deployment/MinGW/";
    Wide::Driver::AddMinGWIncludePaths(copts, MinGWInstallPath);
#else
    Wide::Driver::AddLinuxIncludePaths(copts);
#endif

    auto AddStdlibLink = [&](llvm::ExecutionEngine* ee, llvm::Module* m) {
#ifdef _MSC_VER
        std::string err;
        auto libpath = MinGWInstallPath + "bin/";
        for (auto lib : { "libgcc_s_dw2-1.dll", "libstdc++-6.dll" }) {
            if (llvm::sys::DynamicLibrary::LoadLibraryPermanently((libpath + lib).c_str(), &err))
                __debugbreak();
        }
#endif
        for (auto global_it = m->global_begin(); global_it != m->global_end(); ++global_it) {
            auto&& global = *global_it;
            auto name = global.getName().str();
            if (auto addr = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(global.getName().str().c_str()))
                ee->addGlobalMapping(&global, addr);
        }
    };
    static const auto loc = Wide::Lexer::Range(std::make_shared<std::string>("Test harness internal"));
    llvm::LLVMContext con;
    auto module = Wide::Util::CreateModuleForTriple(copts.TargetOptions.Triple, con);
    auto stdlib = Wide::Driver::SearchStdlibDirectory("../WideLibrary", copts.TargetOptions.Triple);
    std::vector<std::string> files(stdlib.begin(), stdlib.end());
    files.push_back(file);
    copts.HeaderSearchOptions->AddPath("../WideLibrary", clang::frontend::IncludeDirGroup::System, false, false);
    llvm::Function* main = nullptr;
    Wide::Driver::Compile(copts, files, con, [&](Wide::Semantic::Analyzer& a, const Wide::Parse::Module* root) {
        Wide::Semantic::AnalyzeExportedFunctions(a);
        auto m = Wide::Semantic::Type::AccessMember(Wide::Semantic::Expression::NoInstance(), a.GetGlobalModule()->BuildValueConstruction(Wide::Semantic::Expression::NoInstance(), {}, { a.GetGlobalModule(), loc }), std::string("Main"), { a.GetGlobalModule(), loc });
        if (!m)
            throw std::runtime_error("No Main() found for test!");
        auto func = dynamic_cast<Wide::Semantic::OverloadSet*>(m->GetType(Wide::Semantic::Expression::NoInstance())->Decay());
        if (!func)
            throw std::runtime_error("Main was not an overload set.");
        auto f = dynamic_cast<Wide::Semantic::Function*>(func->Resolve({}, a.GetGlobalModule()));
        if (!f)
            throw std::runtime_error("Could not resolve Main to a function.");
        f->ComputeBody();
        if (f->GetSignature()->GetReturnType() != a.GetBooleanType())
            throw std::runtime_error("Main did not return bool.");
        a.GenerateCode(module.get());
        std::string mod_ir;
        llvm::raw_string_ostream stream(mod_ir);
        module->print(stream, nullptr);
        stream.flush();
        std::string err;
        llvm::raw_string_ostream errstream(err);
        main = f->EmitCode(module.get());
        if (llvm::verifyModule(*module, &errstream))
            throw std::runtime_error("An LLVM module failed verification.");
    });
    llvm::EngineBuilder b(module.get());
    auto mod = module.get();
    // MCJIT simplifies the code even if you don't ask it to so dump before it's invoked
    std::string mod_ir;
    llvm::raw_string_ostream stream(mod_ir);
    module->print(stream, nullptr);
    stream.flush();
    b.setAllocateGVsWithCode(false);
    b.setUseMCJIT(true);
    b.setEngineKind(llvm::EngineKind::JIT);
    std::string errstring;
    b.setErrorStr(&errstring);
    auto ee = b.create();
    AddStdlibLink(ee, mod);
    // Fuck you, shitty LLVM ownership semantics.
    ee->finalizeObject();
#ifdef _MSC_VER
    ee->runStaticConstructorsDestructors(false);
#endif
    std::string jit_ir;
    llvm::raw_string_ostream jitstream(jit_ir);
    module->print(jitstream, nullptr);
    jitstream.flush();
    auto result = ee->runFunction(main, std::vector<llvm::GenericValue>());
#ifdef _MSC_VER
    ee->runStaticConstructorsDestructors(true);
#endif
    if (ee)
        module.release();
    auto intval = result.IntVal.getLimitedValue();
    if (!intval)
        throw std::runtime_error("Test returned false.");
}

template<typename T> std::pair<std::string, std::function<bool(Wide::Semantic::Error&)>> Handler(std::string name) {
    return{ name, [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::SpecificError<T>*>(&err); } };
}

const std::unordered_map<std::string, std::function<bool(Wide::Semantic::Error& err)>> error_type_strings = {
    Handler<Wide::Semantic::SizeOverrideTooSmall>("SizeOverrideTooSmall"),
    Handler<Wide::Semantic::AlignOverrideTooSmall>("AlignOverrideTooSmall"),
    Handler<Wide::Semantic::ExportNonOverloadSet>("ExportNonOverloadSet"),
    Handler<Wide::Semantic::FunctionArgumentNotType>("FunctionArgumentNotType"),
    Handler<Wide::Semantic::ExplicitThisNoMember>("ExplicitThisNoMember"),
    Handler<Wide::Semantic::ExplicitThisDoesntMatchMember>("ExplicitThisDoesntMatchMember"),
    Handler<Wide::Semantic::ExplicitThisNotFirstArgument>("ExplicitThisNotFirstArgument"),
    Handler<Wide::Semantic::TemplateArgumentNotAType>("TemplateArgumentNotAType"),
    Handler<Wide::Semantic::ImportIdentifierLookupAmbiguous>("ImportIdentifierLookupAmbiguous"),
    Handler<Wide::Semantic::IdentifierLookupAmbiguous>("IdentifierLookupAmbiguous"),
    Handler<Wide::Semantic::CouldNotCreateTemporaryFile>("CouldNotCreateTemporaryFile"),
    Handler<Wide::Semantic::MacroNameNotConstant>("MacroNameNotConstant"),
    Handler<Wide::Semantic::FileNameNotConstant>("FileNameNotConstant"),
    Handler<Wide::Semantic::CPPWrongArgumentNumber>("CPPWrongArgumentNumber"),
    Handler<Wide::Semantic::AmbiguousCPPLookup>("AmbiguousCPPLookup"),
    Handler<Wide::Semantic::UnknownCPPDecl>("UnknownCPPDecl"),
    Handler<Wide::Semantic::TemplateTypeNoCPPConversion>("TemplateTypeNoCPPConversion"),
    Handler<Wide::Semantic::TemplateArgumentNoConversion>("TemplateArgumentNoConversion"),
    Handler<Wide::Semantic::CouldNotInstantiateTemplate>("CouldNotInstantiateTemplate"),
    Handler<Wide::Semantic::CouldNotFindCPPFile>("CouldNotFindCPPFile"),
    Handler<Wide::Semantic::CouldNotTranslateCPPFile>("CouldNotTranslateCPPFile"),
    Handler<Wide::Semantic::CPPFileContainedErrors>("CPPFileContainedErrors"),
    Handler<Wide::Semantic::CouldNotFindMacro>("CouldNotFindMacro"),
    Handler<Wide::Semantic::MacroNotValidExpression>("MacroNotValidExpression"),
    Handler<Wide::Semantic::ArrayWrongArgumentNumber>("ArrayWrongArgumentNumber"),
    Handler<Wide::Semantic::ArrayNotConstantSize>("ArrayNotConstantSize"),
    Handler<Wide::Semantic::NoMemberFound>("NoMemberFound"),
    Handler<Wide::Semantic::IdentifierLookupFailed>("IdentifierLookupFailed"),
    Handler<Wide::Semantic::DynamicCastNotPolymorphicType>("DynamicCastNotPolymorphicType"),
    Handler<Wide::Semantic::VTableLayoutIncompatible>("VTableLayoutIncompatible"),
    Handler<Wide::Semantic::UnimplementedDynamicCast>("UnimplementedDynamicCast"),
    Handler<Wide::Semantic::AddressOfNonLvalue>("AddressOfNonLvalue"),
    Handler<Wide::Semantic::CouldNotInferReturnType>("CouldNotInferReturnType"),
    Handler<Wide::Semantic::InvalidExportSignature>("InvalidExportSignature"),
    Handler<Wide::Semantic::ContinueNoControlFlowStatement>("ContinueNoControlFlowStatement"),
    Handler<Wide::Semantic::BreakNoControlFlowStatement>("BreakNoControlFlowStatement"),
    Handler<Wide::Semantic::VariableAlreadyDefined>("VariableAlreadyDefined"),
    Handler<Wide::Semantic::ExplicitReturnNoType>("ExplicitReturnNoType"),
    Handler<Wide::Semantic::NoMemberToInitialize>("NoMemberToInitialize"),
    Handler<Wide::Semantic::NoBaseToInitialize>("NoBaseToInitialize"),
    Handler<Wide::Semantic::InitializerNotType>("InitializerNotType"),
    Handler<Wide::Semantic::UsingTargetNotConstant>("UsingTargetNotConstant"),
    Handler<Wide::Semantic::OverloadResolutionFailed>("OverloadResolutionFailed"),
    Handler<Wide::Semantic::UnsupportedBinaryExpression>("UnsupportedBinaryExpression"),
    Handler<Wide::Semantic::UnsupportedDeclarationExpression>("UnsupportedDeclarationExpression"),
    Handler<Wide::Semantic::UnsupportedMacroExpression>("UnsupportedMacroExpression"),
    Handler<Wide::Semantic::BaseNotAType>("BaseNotAType"),
    Handler<Wide::Semantic::RecursiveBase>("RecursiveBase"),
    Handler<Wide::Semantic::BaseFinal>("BaseFinal"),
    Handler<Wide::Semantic::MemberNotAType>("MemberNotAType"),
    Handler<Wide::Semantic::RecursiveMember>("RecursiveMember"),
    Handler<Wide::Semantic::AmbiguousMemberLookup>("AmbiguousMemberLookup"),
    Handler<Wide::Semantic::ExportNotSingleFunction>("ExportNotSingleFunction"),
    Handler<Wide::Semantic::AlignmentOverrideNotInteger>("AlignmentOverrideNotInteger"),
    Handler<Wide::Semantic::AlignmentOverrideNotConstant>("AlignmentOverrideNotConstant"),
    Handler<Wide::Semantic::SizeOverrideNotInteger>("SizeOverrideNotInteger"),
    Handler<Wide::Semantic::SizeOverrideNotConstant>("SizeOverrideNotConstant"),
    Handler<Wide::Semantic::ImportNotUnambiguousBase>("ImportNotUnambiguousBase"),
    Handler<Wide::Semantic::ImportNotOverloadSet>("ImportNotOverloadSet"),
    Handler<Wide::Semantic::ImportNotAType>("ImportNotAType"),
    Handler<Wide::Semantic::VirtualOverrideAmbiguous>("VirtualOverrideAmbiguous"),
};

void Wide::Driver::Compile(Wide::Options::Clang& copts, std::string file) {
#ifdef _MSC_VER
    const std::string MinGWInstallPath = "../Deployment/MinGW/";
    Wide::Driver::AddMinGWIncludePaths(copts, MinGWInstallPath);
#else
    Wide::Driver::AddLinuxIncludePaths(copts);
#endif
    std::string name;
    static const auto loc = Wide::Lexer::Range(std::make_shared<std::string>("Test harness internal"));
    llvm::LLVMContext con;
    auto module = Wide::Util::CreateModuleForTriple(copts.TargetOptions.Triple, con);
    auto stdlib = Wide::Driver::SearchStdlibDirectory("../WideLibrary", copts.TargetOptions.Triple);
    std::vector<std::string> files(stdlib.begin(), stdlib.end());
    copts.HeaderSearchOptions->AddPath("../WideLibrary", clang::frontend::IncludeDirGroup::System, false, false);
    files.push_back(file);
    Wide::Driver::Compile(copts, files, con, [&](Wide::Semantic::Analyzer& a, const Wide::Parse::Module* root) {
        auto global = a.GetGlobalModule()->BuildValueConstruction(Wide::Semantic::Expression::NoInstance(), {}, { a.GetGlobalModule(), loc });
        auto failure = Wide::Semantic::Type::AccessMember(Wide::Semantic::Expression::NoInstance(), global, std::string("ExpectedFailure"), { a.GetGlobalModule(), loc });
        if (!failure)
            throw std::runtime_error("Did not find a function indicating what failure was to be expected.");
        auto failureos = dynamic_cast<Wide::Semantic::OverloadSet*>(failure->GetType(Wide::Semantic::Expression::NoInstance())->Decay());
        if (!failureos)
            throw std::runtime_error("ExpectedFailure was not an overload set!");
        auto failfunc = dynamic_cast<Wide::Semantic::Function*>(failureos->Resolve({}, a.GetGlobalModule()));
        if (!failfunc)
            throw std::runtime_error("ExpectedFailure was not a function!");
        failfunc->ComputeBody();
        auto tupty = dynamic_cast<Wide::Semantic::TupleType*>(failfunc->GetSignature()->GetReturnType());
        if (!tupty)
            throw std::runtime_error("ExpectedFailure's result was not a tuple!");
        struct ExpectedFailure {
            std::string type;
            int beginline;
            int begincolumn;
            int endline;
            int endcolumn;
        };
        std::vector<ExpectedFailure> expected_failures;
        auto push_back_lambda = [](std::vector<ExpectedFailure>* vec, const char* type, int bline, int bcolumn, int eline, int ecolumn) {
            vec->push_back({ type, bline, bcolumn, eline, ecolumn });
        };

        // JIT cannot handle aggregate returns so make a trampoline.
        auto tramp = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(con), {}, false), llvm::GlobalValue::LinkageTypes::ExternalLinkage, "tramp", module.get());
        auto call = Wide::Semantic::CreatePrimGlobal(Wide::Range::Empty(), failfunc->GetSignature()->GetReturnType(), [&](Wide::Semantic::CodegenContext& con) {
            return con->CreateCall(failfunc->EmitCode(module.get()));
        });
        std::vector<std::shared_ptr<Wide::Semantic::Expression>> calls;
        for (auto ty : tupty->GetMembers()) {
            auto nested_tupty = dynamic_cast<Wide::Semantic::TupleType*>(ty);
            auto error = tupty->PrimitiveAccessMember(call, calls.size());
            calls.push_back(Wide::Semantic::CreatePrimGlobal(Wide::Range::Empty(), a.GetVoidType(), [&, error](Wide::Semantic::CodegenContext& con) {
                std::vector<llvm::Value*> args;
                args.push_back(con->CreateIntToPtr(
                    llvm::ConstantInt::get(
                        llvm::IntegerType::get(con, sizeof(void*) * 8),
                        (intptr_t)&expected_failures,
                        false), 
                    con.GetInt8PtrTy()));
                for (auto ty : nested_tupty->GetMembers()) {
                    auto member = nested_tupty->PrimitiveAccessMember(error, args.size() - 1)->GetValue(con);
                    if (member->getType() == llvm::IntegerType::get(con, 64))
                        member = con->CreateTrunc(member, llvm::IntegerType::get(con, 32));
                    args.push_back(member);
                }
                std::vector<llvm::Type*> argtypes = { con.GetInt8PtrTy(), con.GetInt8PtrTy(), con->getInt32Ty(), con->getInt32Ty(), con->getInt32Ty(), con->getInt32Ty() };
                return con->CreateCall(
                    con->CreateIntToPtr(
                        llvm::ConstantInt::get(
                        llvm::IntegerType::get(con, sizeof(void*) * 8),
                        (intptr_t)(void(*)(std::vector<ExpectedFailure>*, const char*, int, int, int, int))push_back_lambda,
                        false),
                        llvm::FunctionType::get(con->getVoidTy(), argtypes, false)->getPointerTo()
                    ), 
                    args);
            }));
        }
        Wide::Semantic::CodegenContext::EmitFunctionBody(tramp, {}, [&](Wide::Semantic::CodegenContext& con) {
            for (auto call : calls)
                call->GetValue(con);
            con->CreateRetVoid();
        });
        GenerateCode(module.get(), [&](llvm::ExecutionEngine* ee) {
            ee->finalizeObject();
            auto fptr = (void(*)())ee->getPointerToFunction(tramp);
            fptr();
        });
        if (expected_failures.empty())
            throw std::runtime_error("CompileFail did not expect any failures.");
        for (auto&& expected_err : expected_failures) {
            if (error_type_strings.find(expected_err.type) == error_type_strings.end())
                throw std::runtime_error("CompileFail declared an expected failure of unknown type.");
        }
        auto m = Wide::Semantic::Type::AccessMember(Wide::Semantic::Expression::NoInstance(), global, std::string("Main"), { a.GetGlobalModule(), loc });
        if (!m)
            throw std::runtime_error("No Main() found for test!");
        auto func = dynamic_cast<Wide::Semantic::OverloadSet*>(m->GetType(Wide::Semantic::Expression::NoInstance())->Decay());
        if (!func)
            throw std::runtime_error("Main was not an overload set.");
        auto f = dynamic_cast<Wide::Semantic::Function*>(func->Resolve({}, a.GetGlobalModule()));
        if (!f)
        throw std::runtime_error("Could not resolve Main to a function.");
        f->ComputeBody();
        if (a.errors.empty())
            throw std::runtime_error("CompileFail did not fail.");
        for (auto&& err : a.errors) {
            auto loc = err->location();
            for (auto it = expected_failures.begin(); it != expected_failures.end(); ++it) {
                auto&& expected_err = *it;
                if (error_type_strings.at(expected_err.type)(*err)
                    && loc.begin.line == expected_err.beginline
                    && loc.end.line == expected_err.endline
                    && loc.begin.column == expected_err.begincolumn
                    && loc.end.column == expected_err.endcolumn) 
                {
                    expected_failures.erase(it);
                    break;
                }
            }
        }
        if (!expected_failures.empty())
            throw std::runtime_error("Expected failure was not found.");
    });
}

