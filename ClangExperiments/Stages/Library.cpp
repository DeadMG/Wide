#include "Library.h"
#include "Lexer/lexer.h"
#include "Parser/parser.h"
#include <memory>
#include <vector>
#include <iostream>

using namespace Wide;
using namespace Options;

Library::Library(const Clang& copts, const LLVM& lopts)
    : Generator(lopts, copts)
    , Sema(copts, &Generator)
{
    AddWideFile("WideLibrary/stdlib.wide");
}

void Library::AddWideFile(std::string filename) {
    std::ifstream inputfile(filename, std::ios::binary | std::ios::in);
    std::noskipws(inputfile);
    Wide::Lexer::Arguments largs;
    auto contents = std::string(std::istream_iterator<char>(inputfile), std::istream_iterator<char>());
    Wide::Lexer::Invocation<decltype(contents.begin())> lex(largs, contents.begin(), contents.end());
    Wide::ParseModuleContents(lex, ASTBuilder, ASTBuilder.GlobalModule);
}

void Library::operator()() {
    Sema(ASTBuilder.GlobalModule);
    Generator();
}