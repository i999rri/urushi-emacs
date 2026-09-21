#include <gtest/gtest.h>

#include "Input/Activation.h"

using urusi::core::input::Activation;

namespace
{
    // The window has come to the front once, as it does on starting.
    Activation Started()
    {
        Activation activation;
        activation.Activated();
        return activation;
    }
}

TEST(ActivationTest, FirstActivationTellsEmacsAndResumes)
{
    Activation activation;
    auto actions = activation.Activated();
    EXPECT_TRUE(actions.tellEmacsFocused);
    EXPECT_TRUE(actions.resumeLater);
    EXPECT_TRUE(activation.Active());
}

// A window of the input method comes and goes: XAML says the window has
// gone, but it is still in front. Nothing is to change, least of all the
// input method, which would end what is being composed.
TEST(ActivationTest, GoingThatWindowsDoesNotAgreeWithChangesNothing)
{
    auto activation = Started();
    auto deactivated = activation.Deactivated();
    EXPECT_TRUE(deactivated.checkLater);
    EXPECT_FALSE(deactivated.leaveInputMethod);

    auto checked = activation.DeactivationChecked(true);
    EXPECT_FALSE(checked.leaveInputMethod);
    EXPECT_FALSE(checked.tellEmacsUnfocused);
    EXPECT_TRUE(activation.Active());
}

// Alt+Tab away and back: the input method is told both ways, the coming
// back once XAML has put the focus back.
TEST(ActivationTest, AwayAndBackIsToldBothWays)
{
    auto activation = Started();
    activation.Deactivated();
    auto away = activation.DeactivationChecked(false);
    EXPECT_TRUE(away.leaveInputMethod);
    EXPECT_TRUE(away.tellEmacsUnfocused);
    EXPECT_FALSE(activation.Active());

    auto back = activation.Activated();
    EXPECT_TRUE(back.tellEmacsFocused);
    EXPECT_TRUE(back.resumeLater);
    EXPECT_FALSE(back.enterInputMethod);

    auto resumed = activation.ResumeChecked(true, true);
    EXPECT_TRUE(resumed.enterInputMethod);
}

// Back before the going was looked into: by then it is in front, and it
// never really went.
TEST(ActivationTest, BackBeforeTheCheckIsNoGoing)
{
    auto activation = Started();
    activation.Deactivated();
    auto back = activation.Activated();
    EXPECT_FALSE(back.resumeLater);

    auto checked = activation.DeactivationChecked(true);
    EXPECT_FALSE(checked.leaveInputMethod);
    EXPECT_TRUE(activation.Active());
}

// Told it is active while it already is: the input method is not told
// again, which would end what is being composed.
TEST(ActivationTest, ActivatedAgainDoesNotResume)
{
    auto activation = Started();
    auto again = activation.Activated();
    EXPECT_FALSE(again.resumeLater);
    EXPECT_FALSE(again.enterInputMethod);
}

// The window went again before the coming back was done with, or the
// focus is on something else: the input method is not told the keys
// come here.
TEST(ActivationTest, ResumeOnlyWhereTheKeysCome)
{
    Activation activation;
    EXPECT_FALSE(activation.ResumeChecked(false, true).enterInputMethod);
    EXPECT_FALSE(activation.ResumeChecked(true, false).enterInputMethod);
}

// Checked twice, as two goings were: the second has nothing to add.
TEST(ActivationTest, GoneIsToldOnce)
{
    auto activation = Started();
    activation.Deactivated();
    activation.Deactivated();
    EXPECT_TRUE(activation.DeactivationChecked(false).leaveInputMethod);
    EXPECT_FALSE(activation.DeactivationChecked(false).leaveInputMethod);
}
