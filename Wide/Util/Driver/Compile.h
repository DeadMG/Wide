#pragma once
#include <initializer_list>
#include <string>
#include <vector>
#include <functional>

namespace Wide {
    namespace Options {
        struct Clang;
    }
    namespace Semantic {
        class Analyzer;
    }
    namespace Codegen {
        class Generator;
    }
    namespace Parse {
        struct Module;
    }
    namespace Driver {
        void Compile(const Wide::Options::Clang& copts, const std::vector<std::string>& files, std::function<void(Semantic::Analyzer&, const Parse::Module*)>);
        void Compile(const Wide::Options::Clang& copts, const std::vector<std::string>& files, const std::vector<std::pair<std::string, std::string>>& sources, std::function<void(Semantic::Analyzer&, const Parse::Module*)>);
    }
}