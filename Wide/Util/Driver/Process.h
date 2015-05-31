#pragma once

#include <string>
#include <vector>
#include <Wide/Util/Ranges/Optional.h>

namespace Wide {
	namespace Driver {
		int StartAndWaitForProcess(std::string name, std::vector<std::string> args, Util::optional<unsigned> timeout);
	}
}