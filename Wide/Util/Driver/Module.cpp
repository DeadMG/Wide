#include <Wide/Util/Driver/Module.h>
#include <Wide/Util/Archive.h>
#include <Wide/Util/Paths/Append.h>
#include <boost/uuid/uuid_io.hpp>
#include <Wide/Lexer/Lexer.h>
#include <Wide/Util/Ranges/IteratorRange.h>
#include <Wide/Parser/Parser.h>

#pragma warning(push, 0)
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#pragma warning(pop)

using namespace Wide;
using namespace Driver;

InterfaceModule::InterfaceModule(
    unsigned int version,
    std::string guid,
    std::unordered_set<std::string> implements,
    std::unordered_set<std::string> dependencies,
    std::unordered_set<InterfaceModule*> dependent_interfaces,
    std::string exports,
    std::unordered_map<std::string, std::string> headers
    )
    : Module(version, guid, implements, dependencies)
    , dependent_interfaces(dependent_interfaces)
    , exports(exports)
    , headers(headers)
{}

InterfaceModule::InterfaceModule(Module m, std::unordered_set<InterfaceModule*> deps, std::string exports, std::unordered_map<std::string, std::string> headers)
    : Module(m), dependent_interfaces(deps), exports(exports), headers(headers) {}

namespace {
    void GetAllDependencies(std::unordered_set<InterfaceModule*>& out, const std::unordered_set<InterfaceModule*>& Direct) {
        for (auto dep : Direct) {
            if (out.find(dep) != out.end()) continue;
            out.insert(dep);
            GetAllDependencies(out, dep->GetDependentInterfaces());
        }
    }
}

std::unordered_set<InterfaceModule*> Wide::Driver::GetAllDependencies(const std::unordered_set<InterfaceModule*>& Direct) {
    std::unordered_set<InterfaceModule*> out;
    ::GetAllDependencies(out, Direct);
    return out;
}
std::string Wide::Driver::CreateModuleDef(Module* m) {
    std::string def = "{ \"version\" : " + std::to_string(m->GetVersion()) + ", \"guid\" : " + m->GetGUID() + ", \"dependencies\" : [";
    for (auto&& dep : m->GetDependencyGUIDs()) {
        def += dep;
        def += ", ";
    }
    def += "], \"implements\" : [";
    for (auto&& imp : m->GetImplementedGUIDs()) {
        def += imp;
        def += ", ";
    }
    def += "] }";
    return def;
}
std::string GetDependencyPath(InterfaceModule* dependency) {
    return Wide::Paths::Append("dependencies", dependency->GetGUID());  
}
void ModuleRepository::WriteModuleToFile(Module* m, std::string filepath) {
    Wide::Util::Archive a;
    if (auto imod = dynamic_cast<InterfaceModule*>(m)) {
        a.data["exports.txt"] = imod->GetExports();
        for (auto&& header : imod->GetHeaders()) {
            a.data[Wide::Paths::Append("headers", header.first)] = header.second;
        }
        for (auto dep : Wide::Driver::GetAllDependencies(imod->GetDependentInterfaces())) {
            auto subpath = GetDependencyPath(dep);
            a.data[Wide::Paths::Append(subpath, "module_def.txt")] = CreateModuleDef(dep);
            a.data[Wide::Paths::Append(subpath, "exports.txt")] = dep->exports;
            for (auto&& header : dep->headers) {
                a.data[Wide::Paths::Append(subpath, { "headers", header.first })] = header.second;
            }
        }
    }
    if (auto implmod = dynamic_cast<ImplementationModule*>(m)) {
        a.data[implmod->target] = implmod->LLVMIR;
    }
    a.data["module_def.txt"] = CreateModuleDef(m);
    Wide::Util::WriteToFile(a, filepath);
}
std::pair<InterfaceModule*, ImplementationModule*> ModuleRepository::CreateModule(
    std::string exports,
    std::unordered_map<std::string, std::string> headers,
    std::unordered_set<std::string> implements,
    std::unordered_set<InterfaceModule*> dependencies,
    std::string LLVMIR,
    std::string target,
    unsigned int version
) {
    assert(version == 1);
    auto interface_guid = boost::uuids::to_string(uuid_generator());
    auto implementation_guid = boost::uuids::to_string(uuid_generator());
    
    std::unordered_set<std::string> dependent_guids;
    for (auto dep : GetAllDependencies(dependencies))
        dependent_guids.insert(dep->guid);

    auto interface = std::make_unique<InterfaceModule>(version, interface_guid, implements, dependent_guids, dependencies, exports, headers);
    auto inter_ptr = interface.get();
    auto impl = std::make_unique<ImplementationModule>(version, implementation_guid, implements, dependent_guids, LLVMIR, target);
    auto impl_ptr = impl.get();
    modules[interface_guid] = std::move(interface);
    modules[implementation_guid] = std::move(impl);
    return { inter_ptr, impl_ptr };
}
Module Driver::ParseDefFile(std::string def, std::string filepath) {
    // Universal module variables.
    Wide::Util::optional<unsigned int> version = 1;
    std::unordered_set<std::string> implements;
    Wide::Util::optional<std::string> guid;
    std::unordered_set<std::string> depends;

    Wide::Lexer::Invocation inv(Wide::Range::IteratorRange(def), std::make_shared<std::string>(Wide::Paths::Append(filepath, "module_def.txt")));
    Wide::Parse::PutbackLexer lex(inv);
    lex(&Wide::Lexer::TokenTypes::OpenCurlyBracket);

    while (true) {
        auto string = lex(&Wide::Lexer::TokenTypes::String);
        lex(&Wide::Lexer::TokenTypes::Colon);
        if (string.GetValue() == "version") {
            version = std::stoul(lex(&Wide::Lexer::TokenTypes::Integer).GetValue());
        } else if (string.GetValue() == "guid") {
            guid = lex(&Wide::Lexer::TokenTypes::String).GetValue();
        } else if (string.GetValue() == "implements") {
            lex(&Lexer::TokenTypes::OpenSquareBracket);
            while (true) {
                implements.insert(lex(&Wide::Lexer::TokenTypes::String).GetValue());
                auto continuation = lex({ &Wide::Lexer::TokenTypes::Comma, &Wide::Lexer::TokenTypes::CloseSquareBracket });
                if (continuation.GetType() == &Wide::Lexer::TokenTypes::CloseSquareBracket)
                    break;
            }
        } else if (string.GetValue() == "dependencies") {
            lex(&Lexer::TokenTypes::OpenSquareBracket);
            while (true) {
                depends.insert(lex(&Wide::Lexer::TokenTypes::String).GetValue());
                auto continuation = lex({ &Wide::Lexer::TokenTypes::Comma, &Wide::Lexer::TokenTypes::CloseSquareBracket });
                if (continuation.GetType() == &Wide::Lexer::TokenTypes::CloseSquareBracket)
                    break;
            }
        }
        auto continuation = lex({ &Wide::Lexer::TokenTypes::Comma, &Wide::Lexer::TokenTypes::CloseCurlyBracket });
        if (continuation.GetType() == &Wide::Lexer::TokenTypes::CloseCurlyBracket)
            break;
    }
    if (!guid) throw std::runtime_error("Didn't find a GUID in the module_def.txt file.");
    if (!version) throw std::runtime_error("Didn't find a version in the module_def.txt file.");

    return Module(*version, *guid, implements, depends);
}
Module* ModuleRepository::LoadModuleFromFile(std::string filepath) {
    auto archive = Wide::Util::ReadFromFile(filepath);
    if (archive.data.find("module_def.txt") == archive.data.end())
        throw std::runtime_error("Archive did not have a module_def.txt file, and therefore is not a valid Wide module.");
    auto base = ParseDefFile(archive.data.at("module_def.txt"), Wide::Paths::Append(filepath, "module_def.txt"));

    if (archive.data.find("exports.txt") != archive.data.end()) {
        // Interface module.
        std::string exports = archive.data.at("exports.txt");
        std::unordered_map<std::string, std::string> headers;
        std::unordered_map<std::string, Module> dependent_bases;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> dependent_headers;
        std::unordered_map<std::string, std::string> dependent_exports;
        for (auto&& file : archive.data) {
            if (*llvm::sys::path::begin(file.first) == "headers") {
                llvm::SmallVector<char, 10> fuck;
                llvm::sys::path::append(fuck, ++llvm::sys::path::begin(file.first), llvm::sys::path::end(file.first));
                headers[std::string(fuck.begin(), fuck.end())] = file.second;
            } else if (*llvm::sys::path::begin(file.first) == "dependencies") {
                auto guid = *++llvm::sys::path::begin(file.first);
                if (*++++llvm::sys::path::begin(file.first) == "exports.txt")
                    dependent_exports[guid] = file.second;
                else if (*++++llvm::sys::path::begin(file.first) == "module_def.txt")
                    dependent_bases.insert(std::make_pair(guid, ParseDefFile(file.second, Wide::Paths::Append(filepath, file.first))));
                else if (*++++llvm::sys::path::begin(file.first) == "headers")
                    dependent_headers[guid][*++++++llvm::sys::path::begin(file.first)] = file.second;
                else
                    throw std::runtime_error("Didn't know how to interpret module file- " + Wide::Paths::Append(filepath, file.first));
            } else
                throw std::runtime_error("Didn't know how to interpret module file- " + Wide::Paths::Append(filepath, file.first));
        }
        std::unordered_set<InterfaceModule*> dependencies;
        for (auto guid : base.dependencies) {
            if (dependent_bases.find(guid) == dependent_bases.end()) throw std::runtime_error("Didn't find a module_def.txt in " + filepath + " for module " + guid);
            if (dependent_exports.find(guid) == dependent_exports.end()) throw std::runtime_error("Didn't find an exports.txt in " + filepath + " for module " + guid);
            if (modules.find(guid) != modules.end()) {
                dependencies.insert(dynamic_cast<InterfaceModule*>(modules[guid].get()));
                continue;
            }
            modules[guid] = Wide::Memory::MakeUnique<InterfaceModule>(dependent_bases.at(guid), std::unordered_set<InterfaceModule*>(), dependent_exports[guid], dependent_headers[guid]);
        }
        auto inter_ptr = Wide::Memory::MakeUnique<InterfaceModule>(base, dependencies, exports, headers);
        auto ret = inter_ptr.get();
        modules[base.guid] = std::move(inter_ptr);
        return ret;
    }

    // Implementation module

}