#include <Wide/Util/Driver/Process.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h> 
int Wide::Driver::StartAndWaitForProcess(std::string name, std::vector<std::string> args, Util::optional<unsigned> timeout) {
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<char*> cargs;
        for (auto&& arg : args)
            cargs.push_back(&arg[0]);
        cargs.push_back(nullptr);
        execv(name.c_str(), &cargs[0]);
    }
    int status;
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
}
