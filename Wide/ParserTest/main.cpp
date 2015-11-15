#include <Wide/Parser/Parser.h>
#include <Wide/Lexer/Lexer.h>
#include <Wide/Util/Ranges/StringRange.h>
#include <Wide/Parser/ParserError.h>
#include <Wide/Util/Memory/MakeUnique.h>
#include <Wide/Parser/AST.h>
#include <iostream>
#include <sstream>
#include <unordered_set>

struct TestStatement {
    virtual ~TestStatement() {}
};
struct TestExpression : public TestStatement {};
struct TestAttribute {
    template<typename X, typename Y> TestAttribute(X x, Y y)
    : initialized(Wide::Memory::MakeUnique<X>(std::move(x))), initializer(Wide::Memory::MakeUnique<Y>(std::move(y))) {}
    TestAttribute(TestAttribute&& other)
        : initialized(std::move(other.initialized)), initializer(std::move(other.initializer)) {}
    TestAttribute& operator=(TestAttribute&& other) {
        initialized = std::move(other.initialized);
        initializer = std::move(other.initializer);
        return *this;
    }
    std::unique_ptr<TestExpression> initialized;
    std::unique_ptr<TestExpression> initializer;
};
struct TestFunction;
struct TestAttributes {
    TestAttributes() {}
    TestAttributes(TestAttributes&& other)
    : attributes(std::move(other.attributes)) {}
    TestAttributes& operator=(TestAttributes&& other) {
        attributes = std::move(other.attributes);
        return *this;
    }
    std::vector<TestAttribute> attributes;
    TestAttributes&& operator()(TestAttribute attr) {
        attributes.push_back(std::move(attr));
        return std::move(*this);
    }
    TestFunction operator()(TestFunction f);
};
struct TestFunction {
    TestFunction() {}
    TestFunction(TestFunction&& other)
        : attrs(std::move(other.attrs)) {}
    TestAttributes attrs;
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
        : modules(std::move(mod.modules))
        , overloadsets(std::move(mod.overloadsets)) {}

    TestModule& operator=(TestModule&& other) {
        modules = std::move(other.modules);
        overloadsets = std::move(other.overloadsets);
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
TestFunction TestAttributes::operator()(TestFunction f) {
    f.attrs = std::move(*this);
    return std::move(f);
}
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
struct TestIdentifier : public TestExpression {
    std::string ident;
    TestIdentifier(std::string arg)
        : ident(arg) {}
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
bool operator==(const Wide::Parse::Expression* e, const TestExpression& rhs);
bool operator!=(const Wide::Parse::Expression* e, const TestExpression& rhs) {
    return !(e == rhs);
}
bool operator==(const Wide::Parse::Function* func, const TestFunction& testfunc) {
    if (func->attributes.size() != testfunc.attrs.attributes.size()) return false;
    for (unsigned i = 0; i < func->attributes.size(); ++i) {
        if (func->attributes[i].initialized.get() != *testfunc.attrs.attributes[i].initialized)
            return false;
        if (func->attributes[i].initializer.get() != *testfunc.attrs.attributes[i].initializer)
            return false;
    }
    return true;
}
bool operator==(const std::unordered_set<std::shared_ptr<Wide::Parse::Function>>& overset, const TestOverloadSet& testoverset) {
    std::unordered_map<const Wide::Parse::Function*, const TestFunction*> mapping;
    for (auto func : overset) {
        // Every function should have a 1:1 correspondence with some function in the test overset.
        for (auto&& testfunc : testoverset.functions) {
            if (func.get() == *testfunc) {
                if (mapping.find(func.get()) != mapping.end())
                    return false;
                mapping[func.get()] = testfunc.get();
            }
        }
    }
    return mapping.size() == overset.size();
}
bool operator==(const Wide::Parse::Module* m, const TestModule& rhs) {
    auto result = true;

    for(auto&& decl : m->named_decls) {
        if (auto mod = boost::get<std::pair<Wide::Parse::Access, std::unique_ptr<Wide::Parse::UniqueAccessContainer>>>(&decl.second)) {
            if(rhs.modules.find(decl.first) == rhs.modules.end())
                return false;
            auto underlying_mod = dynamic_cast<Wide::Parse::Module*>(mod->second.get());
            if (!underlying_mod) return false;
            result = result && underlying_mod == *rhs.modules.at(decl.first);
        }
        if (auto overset = boost::get<std::unique_ptr<Wide::Parse::MultipleAccessContainer>>(&decl.second)) {
            if (rhs.overloadsets.find(decl.first) == rhs.overloadsets.end())
                return false;
            auto set = dynamic_cast<Wide::Parse::ModuleOverloadSet<Wide::Parse::Function>*>(overset->get());
            if (!set) return false;
            result = result && set->funcs.at(Wide::Parse::Access::Public) == *rhs.overloadsets.at(decl.first);
        }
    }
    for (auto&& mod : rhs.modules) {
        if (m->named_decls.find(mod.first) == m->named_decls.end())
            return false;
        auto modptr = boost::get<std::pair<Wide::Parse::Access, std::unique_ptr<Wide::Parse::UniqueAccessContainer>>>(&m->named_decls.at(mod.first));
        auto underlying_mod = dynamic_cast<Wide::Parse::Module*>(modptr->second.get());
        if (!underlying_mod) return false;
    }
    
    return result;
}
bool operator==(const Wide::Parse::Statement* s, const TestStatement& rhs);

bool operator==(const Wide::Parse::Tuple* t, const TestTuple& rhs) {
    if (t->expressions.size() != rhs.expressions.size())
        return false;

    auto result = true;
    for (std::size_t i = 0; i < t->expressions.size(); ++i)
        result = result && t->expressions[i].get() == *rhs.expressions[i].get();
    return result;
}

bool operator==(const Wide::Parse::String* s, const TestString& rhs) {
    return s->val == rhs.val;
}
bool operator==(const Wide::Parse::Integer* e, const TestInteger& rhs) {
    return e->integral_value == std::to_string(rhs.val);
}

bool operator==(const Wide::Parse::Expression* e, const TestExpression& rhs) {
    if (auto integer = dynamic_cast<const Wide::Parse::Integer*>(e))
        if (auto testint = dynamic_cast<const TestInteger*>(&rhs))
            return integer == *testint;

    if (auto tup = dynamic_cast<const Wide::Parse::Tuple*>(e))
        if (auto testtup = dynamic_cast<const TestTuple*>(&rhs))
            return tup == *testtup;

    if (auto ident = dynamic_cast<const Wide::Parse::Identifier*>(e))
        if (auto testident = dynamic_cast<const TestIdentifier*>(&rhs))
            return boost::get<std::string>(ident->val) == testident->ident;

    return false;
}

bool operator==(const Wide::Parse::Statement* s, const TestStatement& rhs) {
    if (auto expr = dynamic_cast<const Wide::Parse::Expression*>(s)) 
        if (auto teststmt = dynamic_cast<const TestExpression*>(&rhs))
            return expr == *teststmt;

    return false;
}

struct result {
    unsigned int passed;
    unsigned int failed;
};

template<typename T, typename F> result test(T&& t, F&& f) {
    unsigned testsfailed = 0;
    unsigned testssucceeded = 0;

    for (auto&& test : t) {
        bool failed = false;
        try {
            auto contents = Wide::Range::StringRange(test.second.first);
            Wide::Lexer::Invocation lex(contents, std::make_shared<std::string>(test.first));
            Wide::Parse::Parser parser(lex);
            failed = !f(parser, test.second);
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
        ret["AttrBasic"] = std::make_pair("[attr := initializer]f() {}", TestModule()("f", TestOverloadSet()(TestAttributes()(TestAttribute(TestIdentifier("attr"), TestIdentifier("initializer")))(TestFunction()))));
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

    auto module_test_results = test(module_tests, [](Wide::Parse::Parser& parser, const std::pair<std::string, TestModule>& test) -> bool {
        auto mod = Wide::Memory::MakeUnique<Wide::Parse::Module>(Wide::Util::none);
        auto results = parser.ParseGlobalModuleContents();
        for (auto&& member : results)
            parser.AddMemberToModule(mod.get(), std::move(member));
        return mod.get() == test.second;
    });

    testssucceeded += module_test_results.passed;
    testsfailed += module_test_results.failed;

    auto expression_test_results = test(expression_tests, [](Wide::Parse::Parser& parser, const std::pair<std::string, std::unique_ptr<TestExpression>>& test) -> bool {
        return parser.ParseExpression(nullptr).get() == *test.second.get();
    });

    testssucceeded += expression_test_results.passed;
    testsfailed += expression_test_results.failed;

    std::cout << "Total: " << testssucceeded + testsfailed << " succeeded: " << testssucceeded << " failed: " << testsfailed << "\n";
    return testsfailed != 0;
}