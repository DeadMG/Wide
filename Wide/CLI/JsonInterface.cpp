#include <Wide/CLI/JsonInterface.h>
#include <Wide/CLI/Options.h>
#include <boost/program_options.hpp>
#include <jsonpp/parser.hpp>
#include <jsonpp/value.hpp>
#include <iostream>
#include <fstream>

int Wide::CLI::RunJsonInterface(int argc, const char** argv) {    
    boost::program_options::variables_map input;
    boost::program_options::options_description desc;
    boost::program_options::positional_options_description positional;

    desc.add_options()
        ("interface", boost::program_options::value<std::string>(), "Choose interface- command-line, JSON")
        ("input", boost::program_options::value<std::vector<std::string>>(), "Input JSON files");

    positional.add("input", -1);

    try {
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(desc).positional(positional).run(), input);
    }
    catch (std::exception& e) {
        std::cout << "Malformed command line.\n" << e.what();
        return 1;
    }

    if (!input.count("input")) {
        std::cout << "No input files specified.";
        return 1;
    }

    Wide::CLI::Options opts;
    
    for (auto file : input.at("input").as<std::vector<std::string>>()) {
        std::fstream inputfile(file, std::ios::in);
        json::value val;
        json::parse(inputfile, val);
        for(auto&& pair : val.as<std::map<std::string, json::value>>()) {
            if (pair.first == "Mode") {
                opts.Mode = pair.second.as<std::string>();
            } else if (pair.first == "Triple") {
                opts.Triple = pair.second.as<std::string>();
            } else if (pair.first == "Include") {
                for (auto path : pair.second.as<std::vector<json::value>>())
                    opts.CppIncludePaths.push_back(path.as<std::string>());
            } else if (pair.first == "Source") {
                for (auto file : pair.second.as<std::map<std::string, json::value>>())
                    opts.WideInputFiles.push_back(std::make_pair(file.second["Name"].as<std::string>(), file.second["Contents"].as<std::string>());
            } else if (pair.first == "CppSource") {
                for (auto file : pair.second.as<std::map<std::string, json::value>>())
                    opts.WideInputFiles.push_back(std::make_pair(file.second["Name"].as<std::string>(), file.second["Contents"].as<std::string>());
            } else if (pair.first == "Link") {
                Wide::CLI::LinkOptions linkopts;
                if (!pair.second["Modules"].is<json::null>()) {
                    for(auto mod : pair.second["Modules"].as<std::vector<json::value>>())
                        linkopts.Modules.push_back(mod.as<std::string>());
                }
                if (!pair.second["Output"].is<json::null>()) {
                    linkopts.Output = pair.second["Output"].as<std::string>();
                }
                opts.Mode = linkopts;
            }
        }
    }
}