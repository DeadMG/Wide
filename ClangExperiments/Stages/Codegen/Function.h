#pragma once

#define _SCL_SECURE_NO_WARNINGS

#include <vector>
#include <functional>

namespace llvm {
    class Type;
    class Module;
    class LLVMContext;
}

namespace Wide {
    namespace Codegen {
        class Statement;
		class Generator;
        class Function {
            std::vector<Statement*> statements;
            std::function<llvm::Type*(llvm::Module*)> Type;
            std::string name;
			bool tramp;
        public:
            void AddStatement(Statement* s) {
                statements.push_back(s);
            }
            void Clear() { statements.clear(); }
            
            void EmitCode(llvm::Module*, llvm::LLVMContext& con, Generator&);
                        
            Function(std::function<llvm::Type*(llvm::Module*)> ty, std::string name, bool trampoline = false);
        };
    }
}
