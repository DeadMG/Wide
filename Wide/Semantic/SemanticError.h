#pragma once

#include <Wide/Lexer/Token.h>
#include <stdexcept>
#include <unordered_map>

namespace Wide {
    namespace Semantic {
        enum class Error : int {
            CouldNotFindHeader,
            NoMember
        };
    }
}
#ifndef _MSC_VER
namespace std {
    struct hash<Wide::Semantic::Error> {
        std::size_t operator()(Wide::Parser::Error p) const {
            return hash<int>()((int)p);
        }
    };
}
#endif
namespace Wide {
    namespace Semantic {
        static const std::unordered_map<Error, std::string> ErrorStrings([]() -> std::unordered_map<Error, std::string> {
            std::pair<Error, std::string> strings[] = {
                std::make_pair(Error::CouldNotFindHeader, "Clang could not find the specified header."),
                std::make_pair(Error::NoMember, "The requested member could not be found."),
            };
            return std::unordered_map<Error, std::string>(std::begin(strings), std::end(strings));
        }());
        class SemanticError : public std::exception {
            Lexer::Range where;
            Error err;
        public:
            SemanticError(Lexer::Range loc, Error wha)
                : where(loc), err(wha) {}
            Error error() { return err; }
            Lexer::Range location() { return where; }
            const char* what() const override {
                return ErrorStrings.at(err).c_str();
            }
        };
    }
}
