#pragma once

#include <Wide/CAPI/Lexer.h>
#include <Wide/Parser/Parser.h>

namespace CEquivalents {
    struct Builder;
    struct Combiner {
        std::unordered_set<Builder*> builders;
        Wide::Parse::Combiner combiner;
    };
}