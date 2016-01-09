#include <Wide/Util/Driver/Options.h>
#include <jsonpp/parser.hpp>
#include <jsonpp/value.hpp>
#include <fstream>
#include <boost/program_options.hpp>
#include <Wide/Util/Paths/Exists.h>
#include <Wide/Util/Paths/RemoveFilename.h>
#include <Wide/Util/Paths/GetExecutablePath.h>
#include <Wide/Util/Paths/Append.h>
#include <Wide/Util/Driver/IncludePaths.h>
#include <Wide/Util/Driver/StdlibDirectorySearch.h>
#include <Wide/Util/Driver/Execute.h>
using namespace Wide;
using namespace Driver;

JSONOptions Wide::Driver::ParseJSONOptions(std::string json) {
    JSONOptions opts;
    json::value val;
    json::parse(json, val);
    for (auto&& pair : val.as<std::map<std::string, json::value>>()) {
        if (pair.first == "Triple")
            opts.TargetTriple = pair.second.as<std::string>();
        else if (pair.first == "Include") 
            opts.CppIncludePaths = pair.second.as<std::vector<std::string>>();
        else if (pair.first == "Source")
            for (auto file : pair.second.as<std::vector<std::map<std::string, std::string>>>())
                opts.WideInputFiles.push_back(std::make_pair(file["Name"], file["Contents"]));
        else if (pair.first == "CppSource")
            for (auto file : pair.second.as<std::vector<std::map<std::string, std::string>>>())
                opts.CppInputFiles.push_back(std::make_pair(file["Name"], file["Contents"]));
        else if (pair.first == "Output")
            opts.Output = pair.second.as<std::string>();
        else if (pair.first == "Link") {
            LinkOptions linkopts;
            for (auto&& linkpair : pair.second.as<std::map<std::string, json::value>>()) {
                if (linkpair.first == "Modules") {
                    auto mods = linkpair.second.as<std::vector<std::string>>();
                    linkopts.Modules.insert(linkopts.Modules.end(), mods.begin(), mods.end());
                } else
                    throw std::runtime_error("Unrecognized object key - Link." + linkpair.first);
            }
            opts.Mode = linkopts;
        } else if (pair.first == "Export") {
            ExportOptions exportopts;
            for (auto&& exportpair : pair.second.as<std::map<std::string, json::value>>()) {
                if (exportpair.first == "Include")
                    exportopts.Include = exportpair.second.as<std::vector<std::string>>();
                else
                    throw std::runtime_error("Unrecognized object key - Export." + exportpair.first);
            }
        } else if (pair.first == "RegurgitateSource")
            opts.RegurgitateSource = pair.second.as<bool>();
        else if (pair.first == "StdlibPath")
            opts.StdlibPath = pair.second.as<std::string>();
        else if (pair.first == "GCCPath")
            opts.GCCPath = pair.second.as<std::string>();
        else
            throw std::runtime_error("Unrecognized object key " + pair.first);
    }
    return opts;
}
namespace {
    boost::program_options::options_description GetCLIOptions() {
        boost::program_options::options_description desc;
        desc.add_options()
            ("help", "Print all available options")
            ("gcc", boost::program_options::value<std::string>(), "The GCC executable. Defaulted to g++.")
            ("triple", boost::program_options::value<std::string>(), "The target triple. Defaulted to "
#ifdef _MSC_VER
                "i686-pc-mingw32.")
#else
                "the LLVM Host triple.")
#endif
            ("input", boost::program_options::value<std::vector<std::string>>(), "One input file. May be specified multiple times.")
            ("version", "Outputs the build of Wide.")
            ("interface", boost::program_options::value<std::string>(), "Permits interface in JSON or text. For help for JSON, specify JSON interface before help.")
            ("stdlib", boost::program_options::value<std::string>(), "The Standard library path. Defaulted to \".\\WideLibrary\\\".")
            ("include", boost::program_options::value<std::vector<std::string>>(), "One include path. May be specified multiple times.")
            ("output", boost::program_options::value<std::string>(), "Specify output file")
            ("mode", boost::program_options::value<std::string>(), "Wide can export a module, or link modules and source into a program")
            ("export-include", boost::program_options::value<std::vector<std::string>>(), "Package files in this directory for inclusion by module consumers")
            ("link-module", boost::program_options::value<std::vector<std::string>>(), "Additional modules to link in to form the final program")
        ;
        return desc;
    }
}

CommandLineOptions Wide::Driver::ParseCommandLineOptions(int argc, char** argv) {
    auto desc = GetCLIOptions();
    boost::program_options::positional_options_description positional;
    positional.add("input", -1);
    boost::program_options::variables_map input;
    boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(desc).positional(positional).run(), input);

    CommandLineOptions opts;
    if (input.count("triple"))
        opts.TargetTriple = input["triple"].as<std::string>();
    if (input.count("version"))
        opts.Version = true;
    if (input.count("gcc"))
        opts.GCCPath = input["gcc"].as<std::string>();
    if (input.count("stdlib"))
        opts.StdlibPath = input["stdlib"].as<std::string>();
    if (input.count("include"))
        opts.CppIncludePaths = input["include"].as<std::vector<std::string>>();
    if (input.count("output"))
        opts.Output = input["output"].as<std::string>();
    if (input.count("help"))
        // WTB to_string for desc
        opts.Help = true;
        //opts.HelpText = 
    if (input.count("input")) 
        opts.WideInputFilepaths = input["input"].as<std::vector<std::string>>();
    if (input.count("mode") && input["mode"].as<std::string>() == "export") {
        ExportOptions exopts;
        if (input.count("export-include"))
            exopts.Include = input["export-include"].as<std::vector<std::string>>();
        opts.Mode = exopts;
    } else {
        LinkOptions linkopts;
        if (input.count("link-module"))
            linkopts.Modules = input["link-module"].as<std::vector<std::string>>();
        opts.Mode = linkopts;
    }

    return opts;
}

std::string Wide::Driver::GetDefaultOutputFile() {
    return "a.o";
}
#ifdef _MSC_VER
std::string Wide::Driver::GetDefaultTargetTriple() {
    return "i686-pc-mingw32";
}
#else
std::string Wide::Driver::GetDefaultTargetTriple() {
    return llvm::sys::getDefaultTargetTriple();
}
#endif
namespace {
    std::string GetDefaultPath(std::string what) {
        if (Wide::Paths::Exists("./" + what))
            return "./" + what;
        auto executablepath = Wide::Paths::GetExecutablePath();
        auto without_file = Wide::Paths::RemoveFilename(executablepath);
        auto with_lib = Wide::Paths::Append(without_file, what);
        if (Wide::Paths::Exists(with_lib))
            return with_lib;
        throw std::runtime_error("Could not find " + what + " at ./" + what + " or at " + with_lib);
    }
}
std::string Wide::Driver::GetDefaultStdlibPath() {
    return GetDefaultPath("WideLibrary");
}
std::string Wide::Driver::GetDefaultGCC() {
#ifdef _MSC_VER
    return Wide::Paths::Append(GetDefaultPath("MinGW"), { "bin", "g++" });
#else
    return "g++";
#endif
}
DriverOptions Wide::Driver::Translate(UserSpecifiedOptions inopts) {
    DriverOptions outopts;
    outopts.OutputFile = inopts.Output ? *inopts.Output : GetDefaultOutputFile();
    outopts.TargetTriple = inopts.TargetTriple ? *inopts.TargetTriple : GetDefaultTargetTriple();
    outopts.CppIncludePaths = Wide::Driver::GetGCCIncludePaths(Wide::Driver::GetDefaultGCC());
    outopts.CppIncludePaths.insert(outopts.CppIncludePaths.end(), inopts.CppIncludePaths.begin(), inopts.CppIncludePaths.end());
    outopts.Mode = inopts.Mode;
    auto stdlibpath = inopts.StdlibPath ? *inopts.StdlibPath : GetDefaultStdlibPath();
    outopts.CppIncludePaths.push_back(stdlibpath);
    auto files = Wide::Driver::SearchStdlibDirectory(stdlibpath, outopts.TargetTriple);
    for (auto&& file : files) {
        std::fstream infile(file, std::ios::in | std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
        outopts.WideInputFiles.push_back(std::make_pair(file, contents));
    }
    return outopts;
}
DriverOptions Wide::Driver::Translate(JSONOptions jopts) {
    auto driveropts = Translate((UserSpecifiedOptions&)jopts);
    if (jopts.WideInputFiles.empty())
        throw std::runtime_error("JSON did not specify any Wide input files");
    driveropts.WideInputFiles.insert(driveropts.WideInputFiles.end(), jopts.WideInputFiles.begin(), jopts.WideInputFiles.end());
    for (auto&& pair : jopts.CppInputFiles) {
        std::fstream tempfile(pair.first, std::ios::out | std::ios::binary);
        tempfile.write(pair.second.c_str(), pair.second.size());
        tempfile.flush();
        driveropts.CppInputFiles.push_back(pair.first);
    }
    return driveropts;
}
boost::variant<DriverOptions, std::string> Wide::Driver::Translate(CommandLineOptions cliopts) {
    if (cliopts.Help)
        return static_cast<std::stringstream&>(std::stringstream() << GetCLIOptions()).str();
    if (cliopts.Version)
        return "Local development build, date " __DATE__ " time " __TIMESTAMP__ ".\n";
    if (cliopts.WideInputFilepaths.empty())
        throw std::runtime_error("JSON did not specify any Wide input files");
    auto driveropts = Translate((UserSpecifiedOptions&)cliopts);
    for (auto&& path : cliopts.WideInputFilepaths) {
        std::fstream infile(path, std::ios::in | std::ios::binary);
        auto contents = std::string(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
        driveropts.WideInputFiles.push_back(std::make_pair(path, contents));
    }
    return driveropts;
}
std::string Wide::Driver::TextualResponse(const Response& r) {
    return std::string();
}
namespace {
    json::value JsonRange(Lexer::Range r) {
        json::value where;
        where["Filename"] = *r.begin.name;
        where["Begin"]["Line"] = r.begin.line;
        where["Begin"]["Column"] = r.begin.column;
        where["End"]["Line"] = r.end.line;
        where["End"]["Column"] = r.end.column;
        return where;
    }
}
std::string Wide::Driver::JsonResponse(const Response& r, JSONOptions opts) {
    json::value response;
    if (opts.RegurgitateSource) {
        // Should be enough to refill the playground's state from a share.
        std::vector<json::value> sources;
        for (auto&& src : opts.WideInputFiles) {
            json::value source;
            source["Name"] = src.first;
            source["Contents"] = src.second;
            sources.push_back(source);
        }
        response["Source"] = sources;
        std::vector<json::value> cppsources;
        for (auto&& src : opts.CppInputFiles) {
            json::value source;
            source["Name"] = src.first;
            source["Contents"] = src.second;
            cppsources.push_back(source);
        }
        response["CppSource"] = cppsources;
    }
    std::vector<json::value> errors;
    for (auto&& err : r.Lex.LexErrors) {
        json::value error;
        error["Where"] = JsonRange(err.Where);
        switch (err.What) {
        case Lexer::Failure::UnlexableCharacter:
            error["What"] = "Unlexable character";
            break;
        case Lexer::Failure::UnterminatedComment:
            error["What"] = "Unterminated comment";
            break;
        case Lexer::Failure::UnterminatedStringLiteral:
            error["What"] = "Unterminated string literal";
            break;
        }
        errors.push_back(error);
    }
    for (auto&& err : r.Parse.ParseErrors) {
        json::value error;
        error["Where"] = JsonRange(err.GetInvalidToken() ? err.GetInvalidToken()->GetLocation() : err.GetLastValidToken().GetLocation());
        error["What"] = err.what();
        errors.push_back(error);
    }
    for (auto&& err : r.Parse.ASTErrors) {
        json::value error;
        error["What"] = err.what();
        errors.push_back(error);
    }
    for (auto&& err : r.Combine.ASTErrors) {
        json::value error;
        error["What"] = err.what();
        errors.push_back(error);
    }
    for (auto&& err : r.Analysis.AnalysisErrors) {
        json::value error;
        error["What"] = err.what();
        error["Where"] = JsonRange(err.location());
        errors.push_back(error);
    }
    for (auto&& err : r.Analysis.ClangDiagnostics) {
        json::value error;
        error["What"] = err.what;
        std::vector<json::value> locations;
        for (auto loc : err.where)
            locations.push_back(JsonRange(loc));
        error["Where"] = locations;
        error["Severity"] = err.severity;
        errors.push_back(error);
    }
    if (r.Analysis.OldError) {
        json::value error;
        error["What"] = r.Analysis.OldError->what();
        errors.push_back(error);
    }
    response["Errors"] = errors;
    return json::dump_string(response);
}