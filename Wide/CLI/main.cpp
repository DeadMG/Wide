#include <boost/program_options.hpp>
#include <iterator>
#include <fstream>
#include <Wide/Util/Driver/Execute.h>
#include <iostream> 

int main(int argc, char** argv)
{
    boost::program_options::options_description initial_desc;
    initial_desc.add_options()
        ("interface", boost::program_options::value<std::string>(), "Choose interface- command-line, JSON")
        ("jsoninput", boost::program_options::value<std::string>(), "JSON input file");
    boost::program_options::variables_map initial_input;

    try {
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(initial_desc).allow_unregistered().run(), initial_input);
    }
    catch (std::exception& e) {
        std::cout << "Malformed command line.\n" << e.what();
        return 1;
    }

    if (initial_input.count("interface") && initial_input["interface"].as<std::string>() == "JSON") {
        try {
            std::fstream file(initial_input["jsoninput"].as<std::string>(), std::ios::in | std::ios::binary);
            auto json = std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            auto useropts = Wide::Driver::ParseJSONOptions(json);
            auto driveropts = Wide::Driver::Translate(useropts);
            auto response = Wide::Driver::Execute(driveropts);
            auto response_text = Wide::Driver::JsonResponse(response, useropts);
            std::cout << response_text;
            return Wide::Driver::Failed(response);
        } catch (std::exception& e) {
            std::cout << "Could not interpret JSON options\n" << e.what();
            return 1;
        }
    }

    if (initial_input.count("interface") && initial_input["interface"].as<std::string>() != "CLI") {
        std::cout << "Unrecognized interface " << initial_input["interface"].as<std::string>() << " expected JSON or CLI";
        return 1;
    }

    try {
        auto useropts = Wide::Driver::ParseCommandLineOptions(argc, argv);
        auto translated_useropts = Wide::Driver::Translate(useropts);
        if (auto str = boost::get<std::string>(&translated_useropts)) {
            std::cout << *str;
            return 0;
        }
        auto driveropts = boost::get<Wide::Driver::DriverOptions>(translated_useropts);
        auto response = Wide::Driver::Execute(driveropts);
        auto response_text = Wide::Driver::TextualResponse(response);
        std::cout << response_text;
        return Wide::Driver::Failed(response);
    } catch (std::exception& e) {
        std::cout << "Could not interpret CLI options\n" << e.what();
        return 1;
    }
}
