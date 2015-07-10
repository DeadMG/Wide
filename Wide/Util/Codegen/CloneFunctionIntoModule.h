#pragma once

namespace llvm {
	class Function;
	class Module;
}
namespace Wide {
	namespace Util {
		llvm::Function* CloneFunctionIntoModule(llvm::Function* src, llvm::Module* dest);
	}
}