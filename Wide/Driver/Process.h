#pragma once

#include <string>
#include <vector>
#include <Wide/Util/Ranges/Optional.h>

namespace Wide {
	namespace Driver {
        struct ProcessResult {
            int exitcode;
            std::string std_out;
            std::string std_err;
        };
        ProcessResult StartAndWaitForProcess(std::string name, std::vector<std::string> args, Util::optional<unsigned> timeout);
	}
}