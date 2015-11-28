#include <Wide/Util/Driver/Process.h>

#ifndef _MSC_VER 
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h> 
int Wide::Driver::StartAndWaitForProcess(std::string name, std::vector<std::string> args, Util::optional<unsigned> timeout) {
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<const char*> cargs;
        cargs.push_back(name.c_str());
        for (auto&& arg : args)
            cargs.push_back(arg.c_str());
        cargs.push_back(nullptr);
        execv(name.c_str(), const_cast<char* const*>(&cargs[0]));
    }
    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status))
        return 1;
    return WEXITSTATUS(status);
}
#else
#include <Windows.h>

int Wide::Driver::StartAndWaitForProcess(std::string name, std::vector<std::string> args, Util::optional<unsigned> timeout)
{
    PROCESS_INFORMATION info = { 0 };
    STARTUPINFO startinfo = { sizeof(STARTUPINFO) };
    std::string final_args = name;
    for (auto arg : args)
         final_args += " " + arg;
    auto proc = CreateProcess(
        name.c_str(),
        &final_args[0],
        nullptr,
        nullptr,
        FALSE,
        NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startinfo,
        &info
         );
    if (!proc) {
        DWORD dw = GetLastError();
        const char* message;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&message, 0, nullptr);
        std::string err = message;
        LocalFree((void*)message);
        throw std::runtime_error(err);        
    }
    if (timeout == 0)
        timeout = INFINITE;

    if (WaitForSingleObject(info.hProcess, timeout ? *timeout : INFINITE) == WAIT_TIMEOUT)
         TerminateProcess(info.hProcess, 1);

    DWORD exit_code;
    GetExitCodeProcess(info.hProcess, &exit_code);
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    return exit_code;
}
#endif
