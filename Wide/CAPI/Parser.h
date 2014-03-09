#pragma once

#include <Wide/CAPI/Lexer.h>
#include <Wide/Parser/Parser.h>
#include <Wide/Parser/Builder.h>

namespace CEquivalents {
    struct Builder;
    struct Combiner {
        std::unordered_set<Builder*> builders;
        Wide::AST::Combiner combiner;
    };
}