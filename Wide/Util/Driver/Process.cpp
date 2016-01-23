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

class Pipe {
    HANDLE ReadHandle;
    HANDLE writehandle;
public:
    Pipe() {
        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;
        CreatePipe(&ReadHandle, &writehandle, &saAttr, 0);
    }
    HANDLE WriteHandle() {
        return writehandle;
    }
    std::string Contents() {
        CloseHandle(writehandle);
        DWORD dwRead;
        CHAR chBuf[1024];
        BOOL bSuccess = FALSE;

        std::string result;
        for (;;)
        {
            bSuccess = ReadFile(ReadHandle, chBuf, 1024, &dwRead, NULL);
            if (!bSuccess || dwRead == 0) break;
            result += std::string(chBuf, chBuf + dwRead);
        }
        return result;
    }
    ~Pipe() {
        CloseHandle(ReadHandle);
    }
};
Wide::Driver::ProcessResult Wide::Driver::StartAndWaitForProcess(std::string name, std::vector<std::string> args, Util::optional<unsigned> timeout)
{
    ProcessResult result;
    Pipe stdoutpipe;
    PROCESS_INFORMATION info = { 0 };
    STARTUPINFO startinfo = { sizeof(STARTUPINFO) };
    std::string final_args = name;
    for (auto arg : args)
         final_args += " " + arg;
    startinfo.hStdOutput = stdoutpipe.WriteHandle();
    startinfo.hStdError = INVALID_HANDLE_VALUE;
    startinfo.hStdInput = INVALID_HANDLE_VALUE;
    startinfo.dwFlags |= STARTF_USESTDHANDLES;
    auto proc = CreateProcess(
        name.c_str(),
        &final_args[0],
        nullptr,
        nullptr,
        TRUE,
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

    result.std_out = stdoutpipe.Contents();
    if (WaitForSingleObject(info.hProcess, timeout ? *timeout : INFINITE) == WAIT_TIMEOUT)
         TerminateProcess(info.hProcess, 1);

    DWORD exit_code;
    GetExitCodeProcess(info.hProcess, &exit_code);
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    result.exitcode = exit_code;
    if (exit_code != 0)
        return result;
    return result;
}
#endif
