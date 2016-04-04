#pragma once

#include <string>
#include <vector>
#include <boost/variant.hpp>
#include <boost/optional.hpp>

namespace Wide {
    namespace Driver {
        struct Response;
        struct LinkOptions {
            std::vector<std::string> Modules;
        };
        struct ExportOptions {
            std::vector<std::string> Include;
        };
        struct UserSpecifiedOptions {
            boost::optional<std::string> Output;
            boost::optional<std::string> StdlibPath;
            boost::optional<std::string> GCCPath;
            boost::optional<std::string> TargetTriple;
            std::vector<std::string> CppIncludePaths;
            boost::variant<LinkOptions, ExportOptions> Mode;
        };
        struct CommandLineOptions : UserSpecifiedOptions {
            bool Version = false;
            bool Help = false;
            std::vector<std::string> WideInputFilepaths;
            std::vector<std::string> CppInputFilepaths;
        };
        struct JSONOptions : UserSpecifiedOptions {
            bool RegurgitateSource = false;
            std::vector<std::pair<std::string, std::string>> WideInputFiles;
            std::vector<std::pair<std::string, std::string>> CppInputFiles;
        };
        struct DriverOptions {
            std::string OutputFile;
            std::string TargetTriple;
            std::vector<std::pair<std::string, std::string>> WideInputFiles;
            std::vector<std::string> CppInputFiles;
            std::vector<std::string> CppIncludePaths;
            boost::variant<LinkOptions, ExportOptions> Mode;
        };
        JSONOptions ParseJSONOptions(std::string json);
        CommandLineOptions ParseCommandLineOptions(int argc, char** argv);

        DriverOptions Translate(UserSpecifiedOptions);
        DriverOptions Translate(JSONOptions);
        boost::variant<DriverOptions, std::string> Translate(CommandLineOptions);
        std::string GetDefaultOutputFile();
        std::string GetDefaultStdlibPath();
        std::string GetDefaultTargetTriple();
        std::string GetDefaultGCC();
        std::string TextualResponse(const Response&);
        std::string JsonResponse(const Response&, JSONOptions);
    }
}