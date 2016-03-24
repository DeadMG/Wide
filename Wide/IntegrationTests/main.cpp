#include <string>
#include <fstream>
#include <jsonpp/parser.hpp>
#include <jsonpp/value.hpp>
#include <unordered_set>
#include <iostream>
#include <Wide/Util/Driver/Process.h>
#include <set>
#include <boost/program_options.hpp>
#include <Wide/Util/DebugBreak.h>
#include <map>

Wide::Driver::ProcessResult ExecuteCompile(json::value test, std::string gccpath, std::string stdlibpath, std::string CLIPath) {
    std::ifstream wide_src(test["wide"].as<std::string>(), std::ios::in | std::ios::binary);
    json::value sourcefile = json::value::object({
        { "Name", test["wide"].as<std::string>() },
        { "Contents", std::string(std::istreambuf_iterator<char>(wide_src), std::istreambuf_iterator<char>()) }
    });
    auto val = json::value::object({
        { "Source", json::value::array({ sourcefile }) },
        { "GCCPath", gccpath },
        { "StdlibPath", stdlibpath }
    });
    if (test["cpp"].is<std::string>()) {
        std::ifstream cpp_src(test["cpp"].as<std::string>(), std::ios::in | std::ios::binary);
        val["CppSource"] = json::value::array({
            json::value::object({
                { "Name", test["cpp"].as<std::string>() },
                { "Contents", std::string(std::istreambuf_iterator<char>(cpp_src), std::istreambuf_iterator<char>()) }
        })
        });
    }
    if (test["cpp"].is<std::vector<std::string>>()) {
        std::vector<json::value> cpp_src_array;
        for (auto&& var : test["cpp"].as<std::vector<std::string>>()) {
            std::ifstream cpp_src(var, std::ios::in | std::ios::binary);
            cpp_src_array.push_back(json::value::object({
                { "Name", var },
                { "Contents", std::string(std::istreambuf_iterator<char>(cpp_src), std::istreambuf_iterator<char>()) }
            }));
        }
        val["CppSource"] = cpp_src_array;

    }
    std::ofstream test_json("current_test.json", std::ios::out | std::ios::trunc);
    auto json_test_input = json::dump_string(val);
    test_json.write(json_test_input.c_str(), json_test_input.size());
    test_json.flush();
    return Wide::Driver::StartAndWaitForProcess(CLIPath, { "--interface=JSON", "--jsoninput=current_test.json" }, 100000);
}

bool MatchesError(json::value expected_error, const json::array& actual_errors) {
    for (auto&& actual_err : actual_errors) {
        auto location = actual_err["Where"];
        if (location["Begin"].as<json::object>()["Line"].as<double>() != expected_error["Begin"].as<json::object>()["Line"].as<double>())
            continue;
        if (location["Begin"].as<json::object>()["Column"].as<double>() != expected_error["Begin"].as<json::object>()["Column"].as<double>())
            continue;
        if (location["End"].as<json::object>()["Line"].as<double>() != expected_error["End"].as<json::object>()["Line"].as<double>())
            continue;
        if (location["End"].as<json::object>()["Column"].as<double>() != expected_error["End"].as<json::object>()["Column"].as<double>())
            continue;
        return true;
    }
    return false;
}

bool ExecuteJsonTest(json::value test) {
#ifdef _MSC_VER
    auto CLIPath = std::string("Wide\\Deployment\\Wide.exe");
#else
    auto CLIPath = std::string("Wide/Deployment/Wide");
#endif
#ifdef _MSC_VER
    auto stdlibpath = std::string("Wide\\WideLibrary");
#else
    auto stdlibpath = std::string("Wide/WideLibrary");
#endif
#ifdef _MSC_VER
    auto gccpath = std::string("Wide\\Deployment\\MinGW\\bin\\g++");
#else
    auto gccpath = std::string("clang++");
#endif
    auto type = test["type"].as<std::string>();
    if (type == "JITSuccess") {
        auto compile = ExecuteCompile(test, gccpath, stdlibpath, CLIPath);
        if (compile.exitcode != 0)
            return compile.exitcode;
#ifndef _MSC_VER
        // Don't support this on Windoze right now- clang and libstdc++ don't always get along
        json::value result;
        json::parse(compile.std_out, result);
        auto errors = result["Errors"].as<json::array>();
        if (!errors.empty())
            return 1;
#endif
#ifdef _MSC_VER
        auto link = Wide::Driver::StartAndWaitForProcess(gccpath + ".exe", { "-o a.exe", "a.o" }, 100000);
#else
        auto link = Wide::Driver::StartAndWaitForProcess(gccpath, { "-o", "a.out", "a.o" }, 100000);
#endif
        if (link.exitcode != 0)
            return link.exitcode;
#ifdef _MSC_VER
        return Wide::Driver::StartAndWaitForProcess("a.exe", {}, 100000).exitcode != 0;
#else
        return Wide::Driver::StartAndWaitForProcess("./a.out", {}, 100000).exitcode != 0;
#endif
    }
    if (type == "CompileFail") {
        auto compile = ExecuteCompile(test, gccpath, stdlibpath, CLIPath);
        if (compile.exitcode != 1)
            return true;
        json::value result;
        json::parse(compile.std_out, result);
        if (!result["Errors"].is<json::array>())
            return true;
        auto errors = result["Errors"].as<json::array>();
        if (!test["expectedErrors"].is<json::array>())
            return true;
        auto expected_errors = test["expectedErrors"].as<json::array>();
        for (auto&& expected_error : expected_errors) {
            if (!MatchesError(expected_error, errors))
                return true;
        }
        return false;
    }
    return false;
}

int main(int argc, char** argv) {
    boost::program_options::options_description desc;
    desc.add_options()
        ("input", boost::program_options::value<std::string>(), "One input file. May be specified multiple times.")
        ("mode", boost::program_options::value<std::string>(), "The testing mode.")
        ("break", "Break when the tests are succeeded to view the output.")
        ;
    boost::program_options::variables_map input;
    try {
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(desc).run(), input);
    }
    catch (std::exception& e) {
        std::cout << "Malformed command line.\n" << e.what();
        return 1;
    }

    auto testspath = std::string("./Wide/IntegrationTests/Tests.json");
    std::fstream jsonfile(testspath, std::ios::in | std::ios::binary);
    std::string json((std::istreambuf_iterator<char>(jsonfile)), std::istreambuf_iterator<char>());
    json::value jsonval;
    json::parse(json, jsonval);
    auto tests = jsonval.as<std::vector<json::value>>();
    std::set<std::string> failures;
    for (auto&& test : tests) {
        auto name = test["name"].is<std::string>() ? test["name"].as<std::string>() : test["wide"].as<std::string>();
        if (input.count("input") && input["input"].as<std::string>() != name)
            continue;
        auto failed = ExecuteJsonTest(test);
        if (failed)
            failures.insert("JSON: " + name);
    }
    if (!failures.empty()) {
        for (auto&& fail : failures)
            std::cout << "Failed: " << fail << "\n";
        std::cout << failures.size() << "failed. " << tests.size() - failures.size() << "succeeded.";
    } else
        std::cout << tests.size() << " completed. 0 failures.";
    if (input.count("break")) {
        int i;
        std::cin >> i;
    }
    return failures.empty() ? 0 : 1;
}
