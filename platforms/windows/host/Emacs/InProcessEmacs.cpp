#include "pch.h"
#include "Emacs/InProcessEmacs.h"

#include "Emacs/EmacsHost.h"
#include "Emacs/HostApi.h"

namespace
{
    // What the standard handle WHICH of this process is: none, or what
    // kind of thing, and whether a child is handed it.
    std::string DescribeHandle(DWORD which)
    {
        HANDLE handle = GetStdHandle(which);
        if (!handle || handle == INVALID_HANDLE_VALUE)
        {
            return "none";
        }

        DWORD flags = 0;
        GetHandleInformation(handle, &flags);
        char const* kinds[] = { "unknown", "disk", "char", "pipe" };
        DWORD kind = GetFileType(handle) & ~FILE_TYPE_REMOTE;
        return std::string{ kind < 4 ? kinds[kind] : "other" }
            + ((flags & HANDLE_FLAG_INHERIT) ? ", inheritable" : "");
    }
}

namespace urusi::windows::emacs
{
    void InProcessEmacs::OnMessage(MessageFn fn)
    {
        HostApi::Instance().OnMessage(std::move(fn));
    }

    // Emacs in this process never exits on its own: kill-emacs ends the
    // whole process, so EXITED is never called.
    void InProcessEmacs::Start(OutputFn output, ExitFn)
    {
        // No -Q: this is the user's Emacs, and it reads the user's init
        // file like any other. Nothing is said here about the screen:
        // site-start.el brings urusi up before the init file, so that
        // the init file can say what the screen should look like, and
        // shows it once the init file has.
        std::vector<std::string> args{ "emacs" };

        // What this process was started with, before any of it is
        // changed. It depends on what started the application, and
        // Emacs hands it on to every program it runs.
        output("host", "started with stdin " + DescribeHandle(STD_INPUT_HANDLE)
               + ", stdout " + DescribeHandle(STD_OUTPUT_HANDLE)
               + ", stderr " + DescribeHandle(STD_ERROR_HANDLE) + "\n");

        // And whether whatever started it handed over a C runtime's
        // table of open files, which the runtime Emacs uses reads as
        // its own descriptors as it starts.
        STARTUPINFOW startup{ sizeof(startup) };
        GetStartupInfoW(&startup);
        output("host", "startup info flags 0x" + std::to_string(startup.dwFlags)
               + ", runtime table " + std::to_string(startup.cbReserved2) + " bytes\n");

        std::string error;
        auto written = [output](std::string text) { output("emacs", std::move(text)); };
        if (!EmacsHost::Instance().Start(EmacsHost::DefaultDll(), args, written, error))
        {
            output("host", error + "\n");
        }
    }

    void InProcessEmacs::Send(std::string const& message)
    {
        HostApi::Instance().Send(message);
    }
}
