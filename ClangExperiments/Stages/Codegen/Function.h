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
        class Function {
            std::vector<Statement*> statements;
            std::function<llvm::Type*(llvm::Module*)> Type;
            std::string name;
        public:
            void AddStatement(Statement* s) {
                statements.push_back(s);
            }
            void Clear() { statements.clear(); }
            
            void EmitCode(llvm::Module*, llvm::LLVMContext& con);
                        
            Function(std::function<llvm::Type*(llvm::Module*)> ty, std::string name);
        };
    }
}
