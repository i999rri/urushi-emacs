#include <gtest/gtest.h>

#include "Emacs/RemoteCommand.h"

using urushi::windows::emacs::RemoteCommand;

TEST(RemoteCommandTest, TheCommandIsTheFirstLineThatIsNotAComment)
{
    EXPECT_EQ(RemoteCommand("# Emacs in WSL\n\n  wsl.exe -e emacs  \r\nsomething else\n"),
              "wsl.exe -e emacs");
}

TEST(RemoteCommandTest, ALineAloneIsTheCommand)
{
    EXPECT_EQ(RemoteCommand("wsl.exe -e emacs"), "wsl.exe -e emacs");
}

TEST(RemoteCommandTest, AByteOrderMarkIsNotPartOfIt)
{
    EXPECT_EQ(RemoteCommand("\xEF\xBB\xBFwsl.exe\r\n"), "wsl.exe");
}

// A file with nothing in it but comments is no command, and Emacs is
// the one in the application.
TEST(RemoteCommandTest, NoCommandIsEmpty)
{
    EXPECT_EQ(RemoteCommand(""), "");
    EXPECT_EQ(RemoteCommand("\r\n  \n# wsl.exe -e emacs\n"), "");
}
