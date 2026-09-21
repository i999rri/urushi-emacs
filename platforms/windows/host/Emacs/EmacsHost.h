#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace urusi::emacs
{
    // Runs Emacs inside this process, from libemacs.dll.
    //
    // Emacs is a program: it expects a main of its own, a thread with
    // the 8 MB stack emacs.exe is linked with, and standard handles to
    // write to. This gives it all three, and hands what it writes to
    // OnOutput so the window can show it. Emacs never returns from the
    // thread in normal use; kill-emacs ends the whole process, so there
    // is nothing to stop and nothing to join.
    class EmacsHost
    {
    public:
        using OutputFn = std::function<void(std::string)>;

        // The one Emacs of this process. It outlives the window on
        // purpose: its threads run until the process ends, and they
        // read from it.
        static EmacsHost& Instance();

        // DLL is the path of libemacs.dll, ARGS the command line Emacs
        // sees, starting with its own name. Returns false and fills
        // ERROR if the DLL cannot be loaded or the thread cannot start.
        bool Start(std::wstring const& dll,
                   std::vector<std::string> const& args,
                   OutputFn onOutput,
                   std::string& error);

        // Where Emacs is: emacs\bin\libemacs.dll in the package, put
        // there by scripts\stage-emacs.sh. URUSI_EMACS_DLL overrides
        // it, to run against a build that is not staged.
        static std::wstring DefaultDll();

    private:
        EmacsHost() = default;

        using InitFn = int(*)(int, char**);

        bool RedirectOutput(std::string& error);
        static DWORD WINAPI RunEmacs(LPVOID self);
        static DWORD WINAPI ReadOutput(LPVOID self);

        InitFn m_init{ nullptr };
        // argv has to outlive the call, and the call is the whole run.
        std::vector<std::string> m_args;
        std::vector<char*> m_argv;
        OutputFn m_onOutput;
        HANDLE m_output{ nullptr };
    };
}
