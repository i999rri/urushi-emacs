#include "pch.h"
#include "Emacs/EmacsProcess.h"

#include "Emacs/RemoteCommand.h"
#include "Text/Lines.h"

#include <string_view>
#include <thread>
#include <vector>

namespace
{
    // Room for a screen or two in each pipe, so that neither side waits
    // on the other for a message while it is being read.
    constexpr DWORD kPipeSize = 1 << 20;

    // The user's own folder, where the file that says how to start Emacs
    // is, and where Emacs is started.
    std::wstring UserProfile()
    {
        wchar_t path[MAX_PATH]{};
        DWORD length = GetEnvironmentVariableW(L"USERPROFILE", path, ARRAYSIZE(path));
        return length && length < ARRAYSIZE(path) ? std::wstring{ path, length } : std::wstring{};
    }

    // The whole of the file at PATH, or nothing if it cannot be read.
    std::string ReadWholeFile(std::wstring const& path)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return {};
        }

        std::string text;
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read)
        {
            text.append(buffer, read);
        }
        CloseHandle(file);
        return text;
    }

    void CloseIfOpen(HANDLE& handle)
    {
        if (handle && handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
        }
        handle = nullptr;
    }

    std::string Hex(DWORD value)
    {
        char text[16];
        sprintf_s(text, "0x%08lX", value);
        return text;
    }
}

namespace urusi::windows::emacs
{
    EmacsProcess::EmacsProcess(std::wstring command) : m_command(std::move(command))
    {
    }

    EmacsProcess::~EmacsProcess()
    {
        CloseIfOpen(m_input);
        CloseIfOpen(m_messages);
        CloseIfOpen(m_errors);
        CloseIfOpen(m_process);
        CloseIfOpen(m_job);
    }

    std::string EmacsProcess::ConfiguredCommand()
    {
        auto home = UserProfile();
        if (home.empty())
        {
            return {};
        }
        return RemoteCommand(ReadWholeFile(home + L"\\.urusi-emacs-remote"));
    }

    void EmacsProcess::OnMessage(MessageFn fn)
    {
        std::lock_guard<std::mutex> held{ m_lock };
        m_onMessage = std::move(fn);
    }

    void EmacsProcess::Start(OutputFn output, ExitFn exited)
    {
        m_output = std::move(output);
        m_exited = std::move(exited);

        m_output("host", "starting Emacs as a process: " + winrt::to_string(m_command) + "\n");
        std::string error;
        if (!Launch(error))
        {
            m_output("host", error + "\n");
            return;
        }

        auto self = shared_from_this();
        // Named, so that what the window spends its time on can be
        // told apart in a profile or in the task manager.
        std::thread{ [self] {
            SetThreadDescription(GetCurrentThread(), L"emacs messages");
            self->ReadMessages();
        } }.detach();
        std::thread{ [self] {
            SetThreadDescription(GetCurrentThread(), L"emacs errors");
            self->ReadErrors();
        } }.detach();
    }

    // Start the process, with pipes for its standard handles and only
    // those handed to it: this process may have other handles a child
    // could be given, and a pipe's end left open in a child is a pipe
    // that never ends.
    bool EmacsProcess::Launch(std::string& error)
    {
        SECURITY_ATTRIBUTES inherited{ sizeof(inherited), nullptr, TRUE };
        HANDLE childInput = nullptr;
        HANDLE childOutput = nullptr;
        HANDLE childErrors = nullptr;
        HANDLE input = nullptr;

        auto fail = [&](char const* what) {
            error = std::string{ what } + " failed: " + Hex(GetLastError());
            CloseIfOpen(input);
            CloseIfOpen(childInput);
            CloseIfOpen(childOutput);
            CloseIfOpen(childErrors);
            return false;
        };

        if (!CreatePipe(&childInput, &input, &inherited, kPipeSize)
            || !CreatePipe(&m_messages, &childOutput, &inherited, kPipeSize)
            || !CreatePipe(&m_errors, &childErrors, &inherited, kPipeSize))
        {
            return fail("CreatePipe");
        }
        SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(m_messages, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(m_errors, HANDLE_FLAG_INHERIT, 0);

        HANDLE handed[] = { childInput, childOutput, childErrors };
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<char> attributes(size);
        auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &size)
            || !UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handed,
                                          sizeof(handed), nullptr, nullptr))
        {
            return fail("the handle list");
        }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = childInput;
        startup.StartupInfo.hStdOutput = childOutput;
        startup.StartupInfo.hStdError = childErrors;
        startup.lpAttributeList = list;

        // The process goes when the application does, however it goes:
        // the job is closed with the last of the application's handles.
        m_job = CreateJobObjectW(nullptr, nullptr);
        if (m_job)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits,
                                    sizeof(limits));
        }

        // CreateProcess may write to the command line it is given.
        std::wstring commandLine = m_command;
        auto home = UserProfile();
        PROCESS_INFORMATION process{};
        BOOL started = CreateProcessW(
            nullptr, commandLine.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
            home.empty() ? nullptr : home.c_str(), &startup.StartupInfo, &process);
        DeleteProcThreadAttributeList(list);
        if (!started)
        {
            return fail("CreateProcess");
        }

        if (m_job && !AssignProcessToJobObject(m_job, process.hProcess))
        {
            m_output("host", "the process is in no job of the application's: "
                     + Hex(GetLastError()) + "\n");
        }
        ResumeThread(process.hThread);
        CloseHandle(process.hThread);
        m_process = process.hProcess;

        // The child has its own ends now, and holding them here would
        // keep them open after it has gone.
        CloseIfOpen(childInput);
        CloseIfOpen(childOutput);
        CloseIfOpen(childErrors);

        // Messages can be sent from now on, and may be being sent from
        // another thread already.
        std::lock_guard<std::mutex> held{ m_writing };
        m_input = input;
        return true;
    }

    // Read what the process writes to its standard output, a message to
    // a line, until it ends, and then say how the process ended.
    void EmacsProcess::ReadMessages()
    {
        urusi::core::text::Lines lines;
        std::vector<char> buffer(64 * 1024);
        DWORD read = 0;

        while (ReadFile(m_messages, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                        nullptr)
               && read)
        {
            lines.Take(std::string_view{ buffer.data(), read },
                       [this](std::string line) { Deliver(std::move(line)); });
        }

        WaitForSingleObject(m_process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(m_process, &code);
        m_output("host", "Emacs exited with " + Hex(code) + "\n");
        if (m_exited)
        {
            m_exited();
        }
    }

    // Give LINE to whatever takes the messages, if it is one. What the
    // shell that starts Emacs says on the way, a login's greeting for
    // one, is written to the same place and is no message: it goes to
    // the log.
    void EmacsProcess::Deliver(std::string line)
    {
        auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos)
        {
            return;
        }
        if (line[first] != '{')
        {
            m_output("emacs", line + "\n");
            return;
        }

        MessageFn handler;
        {
            std::lock_guard<std::mutex> held{ m_lock };
            handler = m_onMessage;
        }
        if (handler)
        {
            handler(std::move(line));
        }
    }

    void EmacsProcess::ReadErrors()
    {
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(m_errors, buffer, sizeof(buffer), &read, nullptr) && read)
        {
            m_output("emacs", std::string{ buffer, read });
        }
    }

    void EmacsProcess::Send(std::string const& message)
    {
        std::lock_guard<std::mutex> held{ m_writing };
        if (!m_input)
        {
            return;
        }

        // Dropped once the process has gone, as HostApi drops what is
        // sent before Emacs listens: there is no one to tell.
        std::string line = message + "\n";
        DWORD offset = 0;
        while (offset < line.size())
        {
            DWORD written = 0;
            if (!WriteFile(m_input, line.data() + offset, static_cast<DWORD>(line.size() - offset),
                           &written, nullptr))
            {
                return;
            }
            offset += written;
        }
    }
}
