#include <Wide/CLI/Link.h>
#include <Wide/Util/Driver/Warnings.h>
#include <Wide/Util/Driver/Compile.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Function.h>
#include <fstream>
#include <boost/program_options.hpp>
#include <llvm/Bitcode/ReaderWriter.h>
#include <Wide/Util/Archive.h>

#pragma warning(push, 0)
#include <llvm/PassManager.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Linker.h>
#pragma warning(pop)

using namespace Wide;
using namespace Driver;

void Driver::AddLinkOptions(boost::program_options::options_description& opts) {
    opts.add_options()
        ("link-output", boost::program_options::value<std::string>(), "The output file. Defaulted to \"a.o\".")
        ("link-module", boost::program_options::value<std::vector<std::string>>(), "Link additional modules.");
}
void Driver::Link(llvm::LLVMContext& con, llvm::Module* mod, std::vector<std::string> files, const Wide::Options::Clang& ClangOpts, const boost::program_options::variables_map& args) {
    std::string outputfile = args.count("link-output") ? args["link-output"].as<std::string>() : "a.o";
    auto inputmodules = args.count("link_module") ? args["link-module"].as<std::vector<std::string>>() : std::vector<std::string>();
    std::vector<std::string> import_bitcodes;
    std::vector<std::pair<std::string, std::string>> imports;
    for (auto file : inputmodules) {
        auto archive = Wide::Util::ReadFromFile(file);
        auto exports = archive.data.at("exports.txt");
        imports.push_back({ exports, file });
        import_bitcodes.push_back(archive.data.at(ClangOpts.TargetOptions.Triple + ".bc"));
    }
    Wide::Driver::Compile(ClangOpts, files, imports, [&](Wide::Semantic::Analyzer& a, const Wide::Parse::Module* root) {
        Wide::Semantic::AnalyzeExportedFunctions(a);
        static const Wide::Lexer::Range location = std::make_shared<std::string>("Analyzer entry point");
        Wide::Semantic::Context c(a.GetGlobalModule(), location);
        auto global = a.GetGlobalModule()->BuildValueConstruction({}, c);
        auto main = Wide::Semantic::Type::AccessMember(std::move(global), std::string("Main"), c);
        if (!main)
            throw std::runtime_error("Could not find Main in global namespace.");
        auto overset = dynamic_cast<Wide::Semantic::OverloadSet*>(main->GetType()->Decay());
        if (!overset)
            throw std::runtime_error("Main in global namespace was not an overload set.");
        auto f = overset->Resolve({}, c.from);
        auto func = dynamic_cast<Wide::Semantic::Function*>(f);
        if (!func)
            throw std::runtime_error("Could not resolve a zero-arguments function from global Main overload set.");
        func->ComputeBody();
        a.GenerateCode(mod);
        // Create main trampoline.
        auto llvmfunc = llvm::Function::Create(llvm::FunctionType::get(llvm::IntegerType::get(con, 32), {}, false), llvm::GlobalValue::LinkageTypes::ExternalLinkage, "main", mod);
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(llvmfunc->getContext(), "entry", llvmfunc);
        llvm::IRBuilder<> builder(entry);
        auto call = builder.CreateCall(func->EmitCode(mod));
        if (call->getType() == llvm::Type::getVoidTy(con))
            builder.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(con), (uint64_t)0, true));
        else if (call->getType() == llvm::Type::getInt64Ty(con))
            builder.CreateRet(builder.CreateTrunc(call, llvm::Type::getInt32Ty(con)));
        else {
            if (call->getType() != llvm::Type::getInt32Ty(con))
                throw std::runtime_error("Main() did not return an int32, int64, or void.");
            builder.CreateRet(call);
        }
        if (llvm::verifyModule(*mod, llvm::VerifierFailureAction::PrintMessageAction))
            throw std::runtime_error("Internal compiler error: An LLVM module failed verification.");
    });
    for (auto bitcode : import_bitcodes) {
        std::string err;
        auto newmod = llvm::ParseBitcodeFile(llvm::MemoryBuffer::getMemBuffer(bitcode), con, &err);
        llvm::Linker::LinkModules(mod, newmod, llvm::Linker::LinkerMode::DestroySource, &err);
    }
    std::string mod_ir;
    llvm::raw_string_ostream stream(mod_ir);
    mod->print(stream, nullptr);
    stream.flush();
    llvm::PassManager pm;
    std::unique_ptr<llvm::TargetMachine> targetmachine;
    llvm::TargetOptions targetopts;
    std::string err;
    const llvm::Target& target = *llvm::TargetRegistry::lookupTarget(ClangOpts.TargetOptions.Triple, err);
    targetmachine = std::unique_ptr<llvm::TargetMachine>(target.createTargetMachine(ClangOpts.TargetOptions.Triple, llvm::Triple(ClangOpts.TargetOptions.Triple).getArchName(), "", targetopts));
    std::ofstream file(outputfile, std::ios::trunc | std::ios::binary);
    llvm::raw_os_ostream out(file);
    llvm::formatted_raw_ostream format_out(out);
    targetmachine->addPassesToEmitFile(pm, format_out, llvm::TargetMachine::CodeGenFileType::CGFT_ObjectFile);
    pm.run(*mod);
}