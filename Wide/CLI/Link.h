#pragma once

#include <vector>
#include <string>

namespace boost {
    namespace program_options {
        class variables_map;
        class options_description;
    }
}
namespace llvm {
    class LLVMContext;
    class Module;
}
namespace Wide {
    namespace Options {
        struct Clang;
    }
    namespace Driver {
        void AddLinkOptions(boost::program_options::options_description&);
        void Link(llvm::LLVMContext&, llvm::Module*, std::vector<std::string>, const Wide::Options::Clang&, const boost::program_options::variables_map&);
    }
}
