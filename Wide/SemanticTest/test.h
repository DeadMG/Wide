#pragma once
#include <string>
#include <unordered_map>
void TestDirectory(std::string path, std::string mode, std::string program, bool debugbreak, std::unordered_map<std::string, std::function<bool()>>& failed);
void Jit(Wide::Options::Clang& copts, std::string file);
void Compile(const Wide::Options::Clang& copts, std::string file);
