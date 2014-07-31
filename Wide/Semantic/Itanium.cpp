#include <Wide/Semantic/Itanium.h>

using namespace Wide;
using namespace Semantic;

llvm::CallingConv::ID Itanium::GetDefaultCallingConvention() {
    return llvm::CallingConv::C;
}
std::string Itanium::GetEHPersonality() {
    return "__gxx_personality_v0";
}