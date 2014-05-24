#include <Wide/Util/Driver/Warnings.h>
#include <Wide/Semantic/Warnings/UnusedFunction.h>
#include <Wide/Parser/AST.h>
#include <iostream>

void Wide::Driver::PrintUnusedFunctions(const std::unordered_set<std::string>& files, Semantic::Analyzer& a) {
    auto unused = Wide::Semantic::GetUnusedFunctions(a);
    for (auto func : unused) {
        if (files.find(*std::get<1>(func).begin.name) != files.end())
            std::cout << "Warning: Unused function " << std::get<2>(func) << " at " << to_string(std::get<1>(func)) << "\n";
    }
}