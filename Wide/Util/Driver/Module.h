#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <boost/uuid/random_generator.hpp>

namespace Wide {
    namespace Driver {
        struct ModuleRepository;
        struct Module {
            friend struct Wide::Driver::ModuleRepository;
            // The version of the Wide module interface this module implements.
        private:
            unsigned int version;
            std::string guid;
            std::unordered_set<std::string> implements;
            std::unordered_set<std::string> dependencies;
        public:
            Module(unsigned int, std::string, std::unordered_set<std::string>, std::unordered_set<std::string>);
            const unsigned int& GetVersion() { return version; }

            // Should be unique for every module.
            const std::string& GetGUID() { return guid; }

            // Kept in a flattened form, this lists all implemented modules, direct or not.
            const std::unordered_set<std::string>& GetImplementedGUIDs() { return implements; }

            // Direct and indirect dependendencies.
            const std::unordered_set<std::string>& GetDependencyGUIDs() { return dependencies; }

            virtual ~Module() {}
        };
        struct InterfaceModule : Module {
        private:
            friend struct Wide::Driver::ModuleRepository;
            // A detailed description of the dependencies of the module.
            std::unordered_set<InterfaceModule*> dependent_interfaces;
            // A description of the Wide code exported by this module.
            std::string exports;
            // A list of the C++ headers describing C++ code exported by this module.
            std::unordered_map<std::string, std::string> headers;
        public:
            InterfaceModule(Module, std::unordered_set<InterfaceModule*>, std::string, std::unordered_map<std::string, std::string>);
            InterfaceModule(unsigned int, std::string, std::unordered_set<std::string>, std::unordered_set<std::string>, std::unordered_set<InterfaceModule*>, std::string, std::unordered_map<std::string, std::string>);
            const std::unordered_set<InterfaceModule*>& GetDependentInterfaces() { return dependent_interfaces; }
            const std::string& GetExports() { return exports; }
            const std::unordered_map<std::string, std::string>& GetHeaders() { return headers; }
        };
        struct ImplementationModule : Module {
            ImplementationModule(unsigned int, std::string, std::unordered_set<std::string>, std::unordered_set<std::string>, std::string LLVMIR, std::string target);
            // The target triple.
            const std::string target;
            // The LLVM IR produced.
            const std::string LLVMIR;
        };
        struct ModuleRepository {
        private:
            std::unordered_map<std::string, std::unique_ptr<Module>> modules;
            boost::uuids::random_generator uuid_generator;
        public:
            Module* LoadModuleFromFile(std::string filepath);
            void WriteModuleToFile(Module* m, std::string filepath);
            std::pair<InterfaceModule*, ImplementationModule*> CreateModule(
                std::string exports,
                std::unordered_map<std::string, std::string> headers,
                std::unordered_set<std::string> implements,
                // Should be only the top-level dependencies, let the repository handle the rest.
                std::unordered_set<InterfaceModule*> dependencies,
                std::string LLVMIR,
                std::string target,
                unsigned int version = 1
            );
        };
        std::string CreateModuleDef(Module* mod);
        std::string GetDependencyPath(InterfaceModule* dependency);
        Module ParseDefFile(std::string def, std::string filepath);
        std::unordered_set<InterfaceModule*> GetAllDependencies(const std::unordered_set<InterfaceModule*>& Direct);
    }
}