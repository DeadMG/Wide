#include <Wide/Util/Paths/GetExecutablePath.h>

#ifdef _MSC_VER
#include <Windows.h>
std::string Wide::Paths::GetExecutablePath() {
    char path[MAX_PATH];
    auto size = GetModuleFileName(NULL, path, MAX_PATH);
    return std::string(path, path + size);
}
#else
std::string Wide::Paths::GetExecutablePath() {
    return "./Wide";
}
#endif