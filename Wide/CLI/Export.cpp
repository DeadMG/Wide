#include <Wide/CLI/Export.h>
#include <Wide/Util/Driver/Warnings.h>
#include <Wide/Util/Driver/Compile.h>
#include <Wide/Semantic/Module.h>
#include <Wide/Semantic/Analyzer.h>
#include <Wide/Semantic/OverloadSet.h>
#include <Wide/Semantic/SemanticError.h>
#include <Wide/Semantic/Function.h>
#include <fstream>
#include <iterator>
#include <istream>
#include <boost/program_options.hpp>
#include <Wide/Semantic/Warnings/GetFunctionName.h>
#include <Wide/Util/Archive.h>
#include <Wide/Semantic/FunctionType.h>

#pragma warning(push, 0)
#include <llvm/PassManager.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#pragma warning(pop)

using namespace Wide;
using namespace Driver;

void Driver::AddExportOptions(boost::program_options::options_description& opts) {
    opts.add_options()
        ("export-output", boost::program_options::value<std::string>(), "The output module filename. Defaulted to \"module.tar.gz\".")
        ("export-module", boost::program_options::value<std::string>(), "The module to be exported.")
        ("export-include", boost::program_options::value<std::string>(), "The directory containing C++ header files to be exported.");
}
namespace {
    std::unordered_set<std::string> SearchHeaders(std::string path) {
        std::unordered_set<std::string> ret;
        auto end = llvm::sys::fs::directory_iterator();
        std::error_code fuck_error_codes;
        bool out = true;
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
                ret.insert(file);
                continue;
            }
            llvm::sys::fs::is_directory(file, isfile);
            if (isfile) {
                auto more = SearchHeaders(file);
                ret.insert(more.begin(), more.end());
            }
        }
        return ret;
    }
    std::string absolute_path(std::string path) {
        llvm::SmallVector<char, 20> llvmpath;
        llvmpath.append(path.begin(), path.end());
        llvm::sys::fs::make_absolute(llvmpath);
        return std::string(llvmpath.begin(), llvmpath.end());
    }
    std::string native_path(std::string path) {
        llvm::SmallVector<char, 20> llvmpath;
        llvm::sys::path::native(path, llvmpath);
        return std::string(llvmpath.begin(), llvmpath.end());
    }
}
void Driver::Export(llvm::LLVMContext& con, llvm::Module* mod, std::vector<std::string> files, const Wide::Options::Clang& ClangOpts, const boost::program_options::variables_map& args) {
    std::string outputfile = args.count("export-output") ? args["export-output"].as<std::string>() : "module.tar.gz";
    std::unordered_map<std::string, std::string> extra_headers;
    if (args.count("export-include")) {
        auto abspath = native_path(absolute_path(args["export-include"].as<std::string>()));
        auto headers = SearchHeaders(abspath);
        for (auto header : headers) {
            auto absheader = native_path(absolute_path(header));
            // Add one to the size to kill the unnecessary separator.
            auto relpath = absheader.substr(abspath.size() + 1);
            std::ifstream stream(absheader, std::ios::in | std::ios::binary);
            std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            extra_headers[relpath] = contents;
        }
    }
    std::string exports;
    Wide::Driver::Compile(ClangOpts, files, [&](Wide::Semantic::Analyzer& a, const Wide::Parse::Module* root) {
        Wide::Semantic::AnalyzeExportedFunctions(a, [&](const Parse::AttributeFunctionBase* func, std::string name, Wide::Semantic::Module* mod) {
            auto semfunc = a.GetWideFunction(func, mod, name);
            a.GetTypeExport(semfunc);
        });

        exports = a.GetTypeExports();
        a.GenerateCode(mod);

        if (llvm::verifyModule(*mod))
            throw std::runtime_error("Internal compiler error: An LLVM module failed verification.");
    });
    Wide::Util::Archive a;
    std::string mod_ir;
    llvm::raw_string_ostream stream(mod_ir);
    llvm::WriteBitcodeToFile(mod, stream);
    stream.flush();
    a.data[ClangOpts.TargetOptions.Triple + ".bc"] = mod_ir;
    a.data["exports.txt"] = exports;
    for (auto&& header : extra_headers) {
        llvm::SmallVector<char, 200> small;
        auto dir = std::string("headers");
        small.append(dir.begin(), dir.end());
        llvm::sys::path::append(small, header.first);
        auto finalpath = std::string(small.begin(), small.end());
        a.data[finalpath] = header.second;
    }
    Wide::Util::WriteToFile(a, outputfile);
}