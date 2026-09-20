#include "pch.h"
#include "EmacsHost.h"

namespace
{
    // Emacs relies on the 8 MB stack emacs.exe is linked with: threads
    // it creates for Lisp take their size from the same request.
    constexpr SIZE_T kEmacsStack = 8 * 1024 * 1024;

    std::wstring ModuleDirectory()
    {
        wchar_t path[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        if (length == 0 || length == ARRAYSIZE(path))
        {
            return {};
        }

        std::wstring directory{ path, length };
        auto slash = directory.find_last_of(L'\\');
        return slash == std::wstring::npos ? std::wstring{} : directory.substr(0, slash);
    }

    std::string LastErrorText(char const* what)
    {
        return std::string{ what } + ": error " + std::to_string(GetLastError());
    }
}

namespace urusi
{
    EmacsHost& EmacsHost::Instance()
    {
        static EmacsHost* instance = new EmacsHost{};
        return *instance;
    }

    std::wstring EmacsHost::DefaultDll()
    {
        wchar_t configured[MAX_PATH]{};
        DWORD length = GetEnvironmentVariableW(L"URUSI_EMACS_DLL", configured, ARRAYSIZE(configured));
        if (length > 0 && length < ARRAYSIZE(configured))
        {
            return std::wstring{ configured, length };
        }

        auto directory = ModuleDirectory();
        return directory.empty() ? L"libemacs.dll" : directory + L"\\emacs\\libemacs.dll";
    }

    bool EmacsHost::Start(std::wstring const& dll,
                          std::vector<std::string> const& args,
                          OutputFn onOutput,
                          std::string& error)
    {
        m_onOutput = std::move(onOutput);

        // Before the DLL is loaded: Emacs's C runtime picks up the
        // standard handles when it starts, which is on that load.
        if (!RedirectOutput(error))
        {
            return false;
        }

        HMODULE module = LoadLibraryW(dll.c_str());
        if (!module)
        {
            error = LastErrorText("cannot load libemacs.dll");
            return false;
        }

        m_init = reinterpret_cast<InitFn>(GetProcAddress(module, "w32_emacs_init"));
        if (!m_init)
        {
            error = "libemacs.dll has no w32_emacs_init";
            return false;
        }

        m_args = args;
        m_argv.clear();
        for (auto& argument : m_args)
        {
            m_argv.push_back(argument.data());
        }
        m_argv.push_back(nullptr);

        HANDLE thread = CreateThread(nullptr, kEmacsStack, RunEmacs, this,
                                     STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (!thread)
        {
            error = LastErrorText("cannot start the Emacs thread");
            return false;
        }
        CloseHandle(thread);

        return true;
    }

    bool EmacsHost::RedirectOutput(std::string& error)
    {
        HANDLE write = nullptr;
        SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        if (!CreatePipe(&m_output, &write, &inheritable, 0))
        {
            error = LastErrorText("cannot create the output pipe");
            return false;
        }

        // A packaged application has no console, so Emacs would write
        // its messages nowhere. Both handles go to one pipe: the order
        // Emacs wrote them in is what makes them readable. The write
        // end is never closed, so the reader below never sees the end
        // of the pipe and keeps up with Emacs for as long as it runs.
        SetStdHandle(STD_OUTPUT_HANDLE, write);
        SetStdHandle(STD_ERROR_HANDLE, write);

        HANDLE reader = CreateThread(nullptr, 0, ReadOutput, this, 0, nullptr);
        if (!reader)
        {
            error = LastErrorText("cannot start the output reader");
            return false;
        }
        CloseHandle(reader);

        return true;
    }

    DWORD WINAPI EmacsHost::RunEmacs(LPVOID parameter)
    {
        auto self = static_cast<EmacsHost*>(parameter);

        // This is Emacs's main thread from here on, and it returns only
        // if Emacs fails to start.
        int status = self->m_init(static_cast<int>(self->m_args.size()), self->m_argv.data());
        if (self->m_onOutput)
        {
            self->m_onOutput("Emacs stopped with status " + std::to_string(status) + "\n");
        }

        return 0;
    }

    DWORD WINAPI EmacsHost::ReadOutput(LPVOID parameter)
    {
        auto self = static_cast<EmacsHost*>(parameter);

        char buffer[4096];
        for (;;)
        {
            DWORD read = 0;
            if (!ReadFile(self->m_output, buffer, sizeof buffer, &read, nullptr) || read == 0)
            {
                return 0;
            }
            if (self->m_onOutput)
            {
                self->m_onOutput(std::string{ buffer, read });
            }
        }
    }
}
