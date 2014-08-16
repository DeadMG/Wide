#pragma once

#include <memory>
#include <string>
#include <boost/variant.hpp>
#include <unordered_map>

namespace Wide {
    namespace Util {
        class Archive {
        public:
            std::unordered_map<std::string, std::string> data;
        };
        Archive ReadFromFile(std::string filepath);
        void WriteToFile(Archive a, std::string filepath);
    }
}