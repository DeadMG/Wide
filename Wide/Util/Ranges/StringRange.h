#pragma once

#include <string>
#include "IteratorRange.h"

namespace Wide {
    namespace Range {
        struct stringrange {
            std::string value;
			std::size_t index;
			stringrange(std::string val)
				: value(std::move(val))
				, index(0) {}
            Util::optional<char> operator()() {
				if (index < value.size())
					return value[index++];
				return Wide::Util::none;
            }
        };
        stringrange StringRange(std::string val) {
            return stringrange(std::move(val));
        }
    }
}