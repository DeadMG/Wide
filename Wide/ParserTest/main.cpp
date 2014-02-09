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
struct TestStatement {
    virtual ~TestStatement() {}
};
struct TestExpression : public TestStatement {};
struct TestInteger : public TestExpression {
    TestInteger(int arg)
    : val(arg) {}
    int val;
};
struct TestString : public TestExpression {
    TestString(std::string arg)
    : val(arg) {}
    std::string val;
};
struct TestTuple : public TestExpression {
    std::vector<std::unique_ptr<TestExpression>> expressions;

    TestTuple() {}
    TestTuple(TestTuple&& other)
        : expressions(std::move(other.expressions)) {}

    TestTuple&& operator()(TestInteger i) {
        expressions.push_back(Wide::Memory::MakeUnique<TestInteger>(i));
        return std::move(*this);
    }
    TestTuple&& operator()(TestString i) {
        expressions.push_back(Wide::Memory::MakeUnique<TestString>(i));
        return std::move(*this);
    }
    TestTuple& operator=(TestTuple&& other) {
        expressions = std::move(other.expressions);
        return *this;
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
    
    return result;
}
bool operator==(const Wide::AST::Statement* s, const TestStatement& rhs);
bool operator==(const Wide::AST::Expression* e, const TestExpression& rhs);

bool operator==(const Wide::AST::Tuple* t, const TestTuple& rhs) {
    if (t->expressions.size() != rhs.expressions.size())
        return false;

    auto result = true;
    for (std::size_t i = 0; i < t->expressions.size(); ++i)
        result = result && t->expressions[i] == *rhs.expressions[i].get();
    return result;
}

bool operator==(const Wide::AST::String* s, const TestString& rhs) {
    return s->val == rhs.val;
}
bool operator==(const Wide::AST::Integer* e, const TestInteger& rhs) {
    return e->integral_value == std::to_string(rhs.val);
}

bool operator==(const Wide::AST::Expression* e, const TestExpression& rhs) {
    if (auto integer = dynamic_cast<const Wide::AST::Integer*>(e))
        if (auto testint = dynamic_cast<const TestInteger*>(&rhs))
            return integer == *testint;

    if (auto tup = dynamic_cast<const Wide::AST::Tuple*>(e))
        if (auto testtup = dynamic_cast<const TestTuple*>(&rhs))
            return tup == *testtup;

    return false;
}

bool operator==(const Wide::AST::Statement* s, const TestStatement& rhs) {
    if (auto expr = dynamic_cast<const Wide::AST::Expression*>(s)) 
        if (auto teststmt = dynamic_cast<const TestExpression*>(&rhs))
            return expr == *teststmt;

    return false;
}

struct result {
    unsigned int passed;
    unsigned int failed;
};

template<typename T, typename F> result test(T&& t, F&& f) {
    auto parserwarninghandler = [](Wide::Lexer::Range where, Wide::Parser::Warning what) {};
    auto parsererrorhandler = [](std::vector<Wide::Lexer::Range> where, Wide::Parser::Error what) {};
    auto combineerrorhandler = [](std::vector<std::pair<Wide::Lexer::Range, Wide::AST::DeclContext*>> errs) {};
    auto parseroutlininghandler = [](Wide::Lexer::Range, Wide::AST::OutliningType) {};

    unsigned testsfailed = 0;
    unsigned testssucceeded = 0;

    for (auto&& test : t) {
        bool failed = false;
        try {
            Wide::Lexer::Arguments largs;
            auto contents = Wide::Range::StringRange(test.second.first);
            Wide::Lexer::Invocation<decltype(contents)> lex(largs, contents, std::make_shared<std::string>(test.first));
            Wide::AST::Builder builder(parsererrorhandler, parserwarninghandler, parseroutlininghandler);
            Wide::Parser::AssumeLexer<decltype(lex)> lexer;
            lexer.lex = &lex;
            Wide::Parser::Parser<decltype(lexer), decltype(builder)> parser(lexer, builder);
            failed = failed || !f(parser, builder, test.second);
        } catch (...) {
            failed = true;
        }
        if (failed) {
            std::cout << "Failed: " << test.first << "\n";
            ++testsfailed;
        } else {
            std::cout << "Succeeded: " << test.first << "\n";
            ++testssucceeded;
        }
    }
    result r = { testssucceeded, testsfailed };
    return r;
}

int main() {
    // Need additional tests for:
    // type() : obj() {}
    // short-form lambdas.
    std::unordered_map<std::string, std::pair<std::string, TestModule>> module_tests = []()->std::unordered_map<std::string, std::pair<std::string, TestModule>> {
        std::unordered_map<std::string, std::pair<std::string, TestModule>> ret;
        ret["ModuleShortForm"] = std::make_pair("module X.Y.Z {}", TestModule()("X", TestModule()("Y", TestModule()("Z", TestModule()))));
        ret["ModuleBasic"] = std::make_pair("module X {}", TestModule()("X", TestModule()));
        ret["FunctionShortForm"] = std::make_pair("X.Y.Z() {}", TestModule()("X", TestModule()("Y", TestModule()("Z", TestOverloadSet()(TestFunction())))));
        return ret;
    }();

    // Expressions
    std::unordered_map<std::string, std::pair<std::string, std::unique_ptr<TestExpression>>> expression_tests = []()->std::unordered_map<std::string, std::pair<std::string, std::unique_ptr<TestExpression>>> {
        std::unordered_map<std::string, std::pair<std::string, std::unique_ptr<TestExpression>>> ret;
        ret["EmptyTuple"] = std::make_pair("{}", Wide::Memory::MakeUnique<TestTuple>());
        ret["OneTuple"] = std::make_pair("{1}", Wide::Memory::MakeUnique<TestTuple>(TestTuple()(TestInteger(1))));
        ret["TwoTuple"] = std::make_pair("{1,2}", Wide::Memory::MakeUnique<TestTuple>(TestTuple()(TestInteger(1))(TestInteger(2))));
        ret["TrailingCommaOne"] = std::make_pair("{1,}", Wide::Memory::MakeUnique<TestTuple>(TestTuple()(TestInteger(1))));
        ret["TrailingCommaTwo"] = std::make_pair("{1,2,}", Wide::Memory::MakeUnique<TestTuple>(TestTuple()(TestInteger(1))(TestInteger(2))));
        return ret;
    }();

    unsigned testsfailed = 0;
    unsigned testssucceeded = 0;

    auto module_test_results = test(module_tests, [](
        Wide::Parser::Parser<Wide::Parser::AssumeLexer<Wide::Lexer::Invocation<Wide::Range::stringrange>>, Wide::AST::Builder>& parser,
        Wide::AST::Builder& builder,
        const std::pair<std::string, TestModule>& test
    ) -> bool {
        parser.ParseGlobalModuleContents(builder.GetGlobalModule());
        return builder.GetGlobalModule() == test.second;
    });

    testssucceeded += module_test_results.passed;
    testsfailed += module_test_results.failed;

    auto expression_test_results = test(expression_tests, [](
        Wide::Parser::Parser<Wide::Parser::AssumeLexer<Wide::Lexer::Invocation<Wide::Range::stringrange>>, Wide::AST::Builder>& parser,
        Wide::AST::Builder& builder,
        const std::pair<std::string, std::unique_ptr<TestExpression>>& test
        ) -> bool {
        return parser.ParseExpression() == *test.second.get();
    });

    testssucceeded += expression_test_results.passed;
    testsfailed += expression_test_results.failed;

    std::cout << "Total: " << testssucceeded + testsfailed << " succeeded: " << testssucceeded << " failed: " << testsfailed << "\n";
    return testsfailed != 0;
}