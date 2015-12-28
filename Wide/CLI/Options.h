#pragma once

#include <string>
#include <vector>
#include <boost/variant.hpp>
#include <boost/optional.hpp>

namespace Wide {
    namespace CLI {
        struct LinkOptions {
            boost::optional<std::string> Output;
            std::vector<std::string> Modules;
        };
        struct ExportOptions {
            boost::optional<std::string> Output;
            boost::optional<std::string> Module;
            boost::optional<std::string> Include;
        };
        struct Options {
            boost::optional<std::string> Triple;
            std::vector<std::pair<std::string, std::string>> WideInputFiles;
            std::vector<std::pair<std::string, std::string>> CppInputFiles;
            std::vector<std::string> CppIncludePaths;
            boost::variant<LinkOptions, ExportOptions> Mode;
        };
    }
}