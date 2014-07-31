#include <Wide/Util/Driver/Compile.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/ClangOptions.h>
#include <Wide/Util/DebugUtilities.h>
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

#pragma warning(push, 0)
#include <llvm/ExecutionEngine/GenericValue.h>
// Gotta include the header or creating JIT won't work... fucking LLVM.
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Analysis/Verifier.h>
#pragma warning(pop)

void TestDirectory(std::string path, std::string mode, std::string program, bool debugbreak, std::unordered_map<std::string, std::function<bool()>>& failedset) {
    unsigned tests_failed = 0;
    unsigned tests_succeeded = 0;
    auto run_test_process = [mode, program, debugbreak](std::string file) {
        auto modearg = "--mode=" + mode;
        std::string arguments = "--input=" + file;
        const char* args[] = { program.c_str(), arguments.c_str(), modearg.c_str(), nullptr };
        std::string err = "";
        bool failed = false;
        auto timeout = debugbreak ? 0 : 10;
        auto ret = llvm::sys::ExecuteAndWait(
            args[0],
            args,
            nullptr,
            nullptr,
            timeout,
            0,
            &err,
            &failed
        );

        return failed || ret;
    };

    auto end = llvm::sys::fs::directory_iterator();
    llvm::error_code fuck_error_codes;
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

void Jit(Wide::Options::Clang& copts, std::string file) {
#ifdef _MSC_VER
    const std::string MinGWInstallPath = "../Deployment/MinGW/";
    Wide::Driver::AddMinGWIncludePaths(copts, MinGWInstallPath);
#else
    Wide::Driver::AddLinuxIncludePaths(copts);
#endif

    auto AddStdlibLink = [&](llvm::ExecutionEngine* ee, llvm::Module* m) {
#ifdef _MSC_VER
        std::string err;
        auto libpath = MinGWInstallPath + "mingw32-dw2/bin/";
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
    Wide::Driver::Compile(copts, [&](Wide::Semantic::Analyzer& a, const Wide::Parse::Module* root) {
        Wide::Semantic::AnalyzeExportedFunctions(a);
        auto m = a.GetGlobalModule()->AccessMember(a.GetGlobalModule()->BuildValueConstruction({}, { a.GetGlobalModule(), loc }), std::string("Main"), { a.GetGlobalModule(), loc });
        if (!m)
            throw std::runtime_error("No Main() found for test!");
        auto func = dynamic_cast<Wide::Semantic::OverloadSet*>(m->GetType()->Decay());
        if (!func)
            throw std::runtime_error("Main was not an overload set.");
        auto f = dynamic_cast<Wide::Semantic::Function*>(func->Resolve({}, a.GetGlobalModule()));
        if (!f)
            throw std::runtime_error("Could not resolve Main to a function.");
        f->ComputeBody();
        if (f->GetSignature()->GetReturnType() != a.GetBooleanType())
            throw std::runtime_error("Main did not return bool.");
        a.GenerateCode(module.get());
        main = f->EmitCode(module.get());
        if (llvm::verifyModule(*module, llvm::VerifierFailureAction::PrintMessageAction))
            throw std::runtime_error("An LLVM module failed verification.");
    }, files);
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
    if (ee)
        module.release();
    ee->finalizeObject();
#ifdef _MSC_VER
    ee->runStaticConstructorsDestructors(false);
#endif
    auto result = ee->runFunction(main, std::vector<llvm::GenericValue>());
#ifdef _MSC_VER
    ee->runStaticConstructorsDestructors(true);
#endif
    auto intval = result.IntVal.getLimitedValue();
    if (!intval)
        throw std::runtime_error("Test returned false.");
}

const std::unordered_map<std::string, std::function<bool(Wide::Semantic::Error& err)>> error_type_strings = {
    { "NoMember", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::NoMember*>(&err); } },
    { "NotAType", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::NotAType*>(&err); } },
    { "CantFindHeader", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::CantFindHeader*>(&err); } },
    { "MacroNotValidExpression", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::MacroNotValidExpression*>(&err); } },
    { "CannotCreateTemporaryFile", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::CannotCreateTemporaryFile*>(&err); } },
    { "UnqualifiedLookupFailure", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::UnqualifiedLookupFailure*>(&err); } },
    { "ClangLookupAmbiguous", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::ClangLookupAmbiguous*>(&err); } },
    { "ClangUnknownDecl", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::ClangUnknownDecl*>(&err); } },
    { "InvalidTemplateArgument", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::InvalidTemplateArgument*>(&err); } },
    { "UnresolvableTemplate", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::UnresolvableTemplate*>(&err); } },
    { "UninstantiableTemplate", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::UninstantiableTemplate*>(&err); } },
    { "CannotTranslateFile", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::CannotTranslateFile*>(&err); } },
    { "IncompleteClangType", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::IncompleteClangType*>(&err); } },
    { "ClangFileParseError", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::ClangFileParseError*>(&err); } },
    { "InvalidBase", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::InvalidBase*>(&err); } },
    { "RecursiveMember", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::RecursiveMember*>(&err); } },
    { "AmbiguousLookup", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::AmbiguousLookup*>(&err); } },
    { "AddressOfNonLvalue", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::AddressOfNonLvalue*>(&err); } },
    { "NoMetaCall", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::NoMetaCall*>(&err); } },
    { "NoMemberToInitialize", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::NoMemberToInitialize*>(&err); } },
    { "ReturnTypeMismatch", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::ReturnTypeMismatch*>(&err); } },
    { "VariableTypeVoid", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::VariableTypeVoid*>(&err); } },
    { "VariableShadowing", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::VariableShadowing*>(&err); } },
    { "TupleUnpackWrongCount", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::TupleUnpackWrongCount*>(&err); } },
    { "NoControlFlowStatement", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::NoControlFlowStatement*>(&err); } },
    { "BadMacroExpression", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::BadMacroExpression*>(&err); } },
    { "BadUsingTarget", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::BadUsingTarget*>(&err); } },
    { "PrologNonAssignment", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::PrologNonAssignment*>(&err); } },
    { "PrologAssignmentNotIdentifier", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::PrologAssignmentNotIdentifier*>(&err); } },
    { "PrologExportNotAString", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::PrologExportNotAString*>(&err); } },
    { "OverloadResolutionFailure", [](Wide::Semantic::Error& err) { return (bool)dynamic_cast<Wide::Semantic::OverloadResolutionFailure*>(&err); } },
};

void Compile(const Wide::Options::Clang& copts, std::string file) {
    std::string name;
    static const auto loc = Wide::Lexer::Range(std::make_shared<std::string>("Test harness internal"));
    llvm::LLVMContext con;
    auto module = Wide::Util::CreateModuleForTriple(copts.TargetOptions.Triple, con);
    Wide::Driver::Compile(copts, [&](Wide::Semantic::Analyzer& a, const Wide::Parse::Module* root) {
        auto global = a.GetGlobalModule()->BuildValueConstruction({}, { a.GetGlobalModule(), loc });
        auto failure = global->GetType()->AccessMember(global, std::string("ExpectedFailure"), { a.GetGlobalModule(), loc });
        if (!failure)
            throw std::runtime_error("Did not find a function indicating what failure was to be expected.");
        auto failureos = dynamic_cast<Wide::Semantic::OverloadSet*>(failure->GetType()->Decay());
        if (!failureos)
            throw std::runtime_error("ExpectedFailure was not an overload set!");
        auto failfunc = dynamic_cast<Wide::Semantic::Function*>(failureos->Resolve({}, a.GetGlobalModule()));
        if (!failfunc)
            throw std::runtime_error("ExpectedFailure was not a function!");
        failfunc->ComputeBody();
        auto tupty = dynamic_cast<Wide::Semantic::TupleType*>(failfunc->GetSignature()->GetReturnType());
        if (!tupty)
            throw std::runtime_error("ExpectedFailure's result was not a tuple!");
        auto str = dynamic_cast<Wide::Semantic::StringType*>(tupty->GetMembers()[0]);
        if (!str)
            throw std::runtime_error("Result of ExpectedFailure's first member was not a string.");

        a.GenerateCode(module.get());

        // JIT cannot handle aggregate returns so check it.
        std::vector<llvm::Type*> types = { llvm::Type::getInt64PtrTy(con), llvm::Type::getInt64PtrTy(con), llvm::Type::getInt64PtrTy(con), llvm::Type::getInt64PtrTy(con) };
        auto tramp = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(con), types, false), llvm::GlobalValue::LinkageTypes::ExternalLinkage, "tramp", module.get());
        auto bb = llvm::BasicBlock::Create(con, "entry", tramp);
        auto builder = llvm::IRBuilder<>(bb);
        auto call = builder.CreateCall(failfunc->EmitCode(module.get()));
        auto current = tramp->arg_begin();
        builder.CreateStore(builder.CreateExtractValue(call, { boost::get<Wide::Semantic::LLVMFieldIndex>(tupty->GetLocation(1)).index }), current++);
        builder.CreateStore(builder.CreateExtractValue(call, { boost::get<Wide::Semantic::LLVMFieldIndex>(tupty->GetLocation(2)).index }), current++);
        builder.CreateStore(builder.CreateExtractValue(call, { boost::get<Wide::Semantic::LLVMFieldIndex>(tupty->GetLocation(3)).index }), current++);
        builder.CreateStore(builder.CreateExtractValue(call, { boost::get<Wide::Semantic::LLVMFieldIndex>(tupty->GetLocation(4)).index }), current++);
        builder.CreateRetVoid();
        int64_t beginline, begincolumn, endline, endcolumn;
        if (llvm::verifyFunction(*tramp, llvm::VerifierFailureAction::PrintMessageAction))
            throw std::runtime_error("Internal Compiler Error: An LLVM function failed verification.");
        if (llvm::verifyModule(*module, llvm::VerifierFailureAction::PrintMessageAction))
            throw std::runtime_error("An LLVM module failed verification.");
        GenerateCode(module.get(), [&](llvm::ExecutionEngine* ee) {
            ee->finalizeObject();
            auto fptr = (void(*)(int64_t*, int64_t*, int64_t*, int64_t*))ee->getPointerToFunction(tramp);
            fptr(&beginline, &begincolumn, &endline, &endcolumn);
        });

        auto m = global->GetType()->AccessMember(global, std::string("Main"), { a.GetGlobalModule(), loc });
        if (!m)
            throw std::runtime_error("No Main() found for test!");
        auto func = dynamic_cast<Wide::Semantic::OverloadSet*>(m->GetType()->Decay());
        if (!func)
            throw std::runtime_error("Main was not an overload set.");
        auto f = dynamic_cast<Wide::Semantic::Function*>(func->Resolve({}, a.GetGlobalModule()));
        if (!f)
            throw std::runtime_error("Could not resolve Main to a function.");
        try {
            f->ComputeBody();
            throw std::runtime_error("CompileFail did not fail.");
        } catch (Wide::Semantic::Error& err) {
            if (error_type_strings.find(str->GetValue()) == error_type_strings.end())
                throw std::runtime_error("Could not find error type string.");
            if (!error_type_strings.at(str->GetValue())(err))
                throw std::runtime_error("The error type string was incorrect.");
            if (err.location().begin.line != beginline)
                throw std::runtime_error("Exception location did not match return from ExpectedFailure!" + to_string(err.location()));
            if (err.location().begin.column != begincolumn)
                throw std::runtime_error("Exception location did not match return from ExpectedFailure!" + to_string(err.location()));
            if (err.location().end.line != endline)
                throw std::runtime_error("Exception location did not match return from ExpectedFailure!" + to_string(err.location()));
            if (err.location().end.column != endcolumn)
                throw std::runtime_error("Exception location did not match return from ExpectedFailure!" + to_string(err.location()));
        }
    }, { file });
}

