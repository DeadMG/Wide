#pragma once
#include <string>
#include <unordered_set>
struct results {
    unsigned passes;
    unsigned fails;
}; 
results TestDirectory(std::string path, std::string mode, std::string program, bool debugbreak, std::unordered_set<std::string>& failed);
void Jit(Wide::Options::Clang& copts, std::string file);
void Compile(const Wide::Options::Clang& copts, std::string file);
