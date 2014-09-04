#include <Wide/CLI/Export.h>
#include <Wide/Util/Driver/Warnings.h>
#include <Wide/Util/Driver/Compile.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Function.h>
#include <fstream>
#include <boost/program_options.hpp>
#include <Wide/Semantic/Warnings/GetFunctionName.h>
#include <Wide/Util/Archive.h>
#include <Wide/Semantic/FunctionType.h>

#pragma warning(push, 0)
#include <llvm/PassManager.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_os_ostream.h>
#pragma warning(pop)

using namespace Wide;
using namespace Driver;

void Driver::AddExportOptions(boost::program_options::options_description& opts) {
    opts.add_options()
        ("export-output", boost::program_options::value<std::string>(), "The output module filename. Defaulted to \"module.tar.gz\".")
        ("export-module", boost::program_options::value<std::string>(), "The module to be exported.")
        ("export-include", boost::program_options::value<std::string>(), "The directory containing C++ header files to be exported.");
}
void Driver::Export(llvm::LLVMContext& con, llvm::Module* mod, std::vector<std::string> files, const Wide::Options::Clang& ClangOpts, const boost::program_options::variables_map& args) {
    std::string outputfile = args.count("export-output") ? args["export-output"].as<std::string>() : "module.tar.gz";
    if (args.count("export-include")) {
        auto folder = args["export-include"].as<std::string>();
        
    }
    std::string exports;
    Wide::Driver::Compile(ClangOpts, files, [&](Wide::Semantic::Analyzer& a, const Wide::Parse::Module* root) {
        //std::vector<std::pair<std::string, Wide::Semantic::Function*>> funcs;
        Wide::Semantic::AnalyzeExportedFunctions(a, [&](const Parse::AttributeFunctionBase* func, std::string name, Wide::Semantic::Module* mod) {
            auto semfunc = a.GetWideFunction(func, mod, name);
            a.GetTypeExport(semfunc);
        });

        exports = a.GetTypeExports();
        a.GenerateCode(mod);

        if (llvm::verifyModule(*mod, llvm::VerifierFailureAction::PrintMessageAction))
            throw std::runtime_error("Internal compiler error: An LLVM module failed verification.");
    });
    Wide::Util::Archive a;
    std::string mod_ir;
    llvm::raw_string_ostream stream(mod_ir);
    llvm::WriteBitcodeToFile(mod, stream);
    stream.flush();
    a.data[ClangOpts.TargetOptions.Triple + ".bc"] = mod_ir;
    a.data["exports.txt"] = exports;
    Wide::Util::WriteToFile(a, outputfile);
}