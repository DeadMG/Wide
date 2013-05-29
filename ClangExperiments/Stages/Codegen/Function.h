#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include <vector>
#include <functional>
#include <unordered_map>
#include <string>

namespace llvm {
    class Type;
    class Module;
    class LLVMContext;
    class Value;
}

namespace Wide {
    namespace Semantic {
        class Function;
    }
    namespace Codegen {
        class Statement;
		class Generator;
        class Function {
            std::vector<Statement*> statements;
            std::function<llvm::Type*(llvm::Module*)> Type;
            std::string name;
			bool tramp;
            std::unordered_map<unsigned, llvm::Value*> ParameterValues;
            Semantic::Function* debug;
        public:
            llvm::Value* GetParameter(unsigned i);
            void AddStatement(Statement* s) {
                statements.push_back(s);
            }
            void Clear() { statements.clear(); }
            
            void EmitCode(llvm::Module*, llvm::LLVMContext& con, Generator&);
                        
            Function(std::function<llvm::Type*(llvm::Module*)> ty, std::string name, Semantic::Function* debug, bool trampoline = false);
        };
    }
}
