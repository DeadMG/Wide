#include <Wide/Parser/Parser.h>
#include <Wide/Lexer/Lexer.h>
#include <Wide/Util/Ranges/StringRange.h>
#include <Wide/Parser/ParserError.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <Wide/Parser/Builder.h>
#include <Wide/Parser/AST.h>
#include <iostream>
#include <sstream>
#include <unordered_set>

struct TestFunction {
};
struct TestOverloadSet {
    std::unordered_set<std::unique_ptr<TestFunction>> functions;
    TestOverloadSet() {}
    TestOverloadSet(TestOverloadSet&& mod)
        : functions(std::move(mod.functions)) {}

    TestOverloadSet& operator=(TestOverloadSet&& other) {
        functions = std::move(other.functions);
        return *this;
    }

    TestOverloadSet&& operator()(TestFunction mod) {
        functions.insert(Wide::Memory::MakeUnique<TestFunction>(std::move(mod)));
        return std::move(*this);
    }
};
struct TestModule {
    std::unordered_map<std::string, std::unique_ptr<TestModule>> modules;
    std::unordered_map<std::string, std::unique_ptr<TestOverloadSet>> overloadsets;

    TestModule() {}
    TestModule(TestModule&& mod)
        : modules(std::move(mod.modules)) {}

    TestModule& operator=(TestModule&& other) {
        modules = std::move(other.modules);
        return *this;
    }

    TestModule&& operator()(std::string str, TestModule mod) {
        modules[str] = Wide::Memory::MakeUnique<TestModule>(std::move(mod));
        return std::move(*this);
    }
    TestModule&& operator()(std::string str, TestOverloadSet mod) {
        overloadsets[str] = Wide::Memory::MakeUnique<TestOverloadSet>(std::move(mod));
        return std::move(*this);
    }
};
bool operator==(const Wide::AST::Module* m, const TestModule& rhs) {
    auto result = true;

    for(auto decl : m->decls) {
        if(auto mod = dynamic_cast<Wide::AST::Module*>(decl.second)) {
            if(rhs.modules.find(decl.first) == rhs.modules.end())
                return false;
            result = result && mod == *rhs.modules.at(decl.first);
        }
    }
    for(auto&& mod : rhs.modules)
    if(m->decls.find(mod.first) == m->decls.end() || !dynamic_cast<Wide::AST::Module*>(m->decls.at(mod.first)))
        result = false;

    for(auto&& set : m->functions) {

    }

    return result;
}

int main() {
    std::unordered_map<std::string, std::pair<std::string, TestModule>> tests = []()->std::unordered_map<std::string, std::pair<std::string, TestModule>> {
        std::unordered_map<std::string, std::pair<std::string, TestModule>> ret;
        ret["ModuleShortForm"] = std::make_pair("module X.Y.Z {}", TestModule()("X", TestModule()("Y", TestModule()("Z", TestModule()))));
        ret["ModuleBasic"] = std::make_pair("module X {}", TestModule()("X", TestModule()));
        ret["FunctionShortForm"] = std::make_pair("X.Y.Z() {}", TestModule()("X", TestModule()("Y", TestModule()("Z", TestOverloadSet()(TestFunction())))));
        return ret;
    }();
    auto parserwarninghandler = [](Wide::Lexer::Range where, Wide::Parser::Warning what) {};
    auto parsererrorhandler = [](std::vector<Wide::Lexer::Range> where, Wide::Parser::Error what) {};
    auto combineerrorhandler = [](std::vector<std::pair<Wide::Lexer::Range, Wide::AST::DeclContext*>> errs) {};
    auto parseroutlininghandler = [](Wide::Lexer::Range, Wide::AST::OutliningType) {};
    unsigned testsfailed = 0;
    unsigned testssucceeded = 0;
    for(auto&& test : tests) {
        bool failed = false;
        try {
            Wide::Lexer::Arguments largs;
            auto contents = Wide::Range::StringRange(test.second.first);
            Wide::Lexer::Invocation<decltype(contents)> lex(largs, contents, std::make_shared<std::string>(test.first));
            Wide::AST::Builder builder(parsererrorhandler, parserwarninghandler, parseroutlininghandler);
            Wide::Parser::AssumeLexer<decltype(lex)> lexer;
            lexer.lex = &lex;
            Wide::Parser::Parser<decltype(lexer), decltype(builder)> parser(lexer, builder);
            parser.ParseGlobalModuleContents(builder.GetGlobalModule());
            failed = failed || !(builder.GetGlobalModule() == test.second.second);
        } catch(...) {
            failed = true;
        }
        if(failed) {
            std::cout << "Failed: " << test.first << "\n";
            ++testsfailed;
        } else {
            std::cout << "Succeeded: " << test.first << "\n";
            ++testssucceeded;
        }
    }
    std::cout << "Total: " << tests.size() << " succeeded: " << testssucceeded << " failed: " << testsfailed << "\n";
    return testsfailed != 0;
}