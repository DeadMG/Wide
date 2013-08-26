#include <Wide/Lexer/Lexer.h>
#include <Wide/Util/Ranges/StringRange.h>
#include <initializer_list>

#define CATCH_CONFIG_MAIN
#include <Wide/Util/Catch.h>

using namespace Wide;

bool LexTokens(std::string contents, std::initializer_list<Lexer::TokenType> types) {
    Lexer::Arguments args;
    Lexer::Invocation<Range::stringrange> inv(args, Range::StringRange(std::move(contents)));
    for(auto type : types) {
        auto val = inv();
        if (!val || val->GetType() != type)
            return false;
    }
    auto val = inv();
    if (val) return false;
    return true;
}

bool LexSingleToken(std::string contents, Lexer::TokenType type) {
    return LexTokens(contents, { type });
}

TEST_CASE("Lexer lexes each token individually", "Lexer") {
    for(auto x : Lexer::Arguments::singles) {
        CHECK(LexSingleToken(std::string(1, x.first), x.second ));
    }

    for(auto first : Lexer::Arguments::doubles) {
        for(auto second : first.second) {
            CHECK(LexSingleToken(std::string(1, first.first) + std::string(1, second.first), second.second));
        }
    }

    for(auto first : Lexer::Arguments::triples) {
        for(auto second : first.second) {
            for(auto third : second.second) {
                CHECK(LexSingleToken(std::string(1, first.first) + std::string(1, second.first) + std::string(1, third.first), third.second));
            }
        }
    }
}

TEST_CASE("Lexer lexes multiple tokens", "Lexer") {
    CHECK(LexTokens(">> >>= <<= <<", { Lexer::TokenType::RightShift, Lexer::TokenType::RightShiftAssign, Lexer::TokenType::LeftShiftAssign, Lexer::TokenType::LeftShift }));
    CHECK(LexTokens("> >> >>=", { Lexer::TokenType::GT, Lexer::TokenType::RightShift, Lexer::TokenType::RightShiftAssign }));
    CHECK(LexTokens("> >> ...", { Lexer::TokenType::GT, Lexer::TokenType::RightShift, Lexer::TokenType::Ellipsis }));
}