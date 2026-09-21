#include <gtest/gtest.h>

#include "Engagement.h"

using urusi::Engagement;
using Action = Engagement::Action;

TEST(EngagementTest, WishBeforeContextIsToldOnceThereIsOne)
{
    Engagement engagement;
    EXPECT_EQ(engagement.Want(true), Action::None);
    EXPECT_TRUE(engagement.Wants());
    EXPECT_FALSE(engagement.IsEngaged());

    EXPECT_EQ(engagement.ContextCreated(), Action::Enter);
    EXPECT_TRUE(engagement.IsEngaged());
}

TEST(EngagementTest, ContextWithoutWishIsToldNothing)
{
    Engagement engagement;
    EXPECT_EQ(engagement.ContextCreated(), Action::None);
    EXPECT_FALSE(engagement.IsEngaged());
}

// The focus coming back to the element, and the window being told it is
// active while it is, as the input method's windows come and go: said
// again, it would end the composition.
TEST(EngagementTest, SayingAgainWhatWasSaidIsSayingNothing)
{
    Engagement engagement;
    engagement.ContextCreated();
    EXPECT_EQ(engagement.Want(true), Action::Enter);
    EXPECT_EQ(engagement.Want(true), Action::None);
    EXPECT_EQ(engagement.Want(false), Action::Leave);
    EXPECT_EQ(engagement.Want(false), Action::None);
}

// Away to another window and back: the input method is told both ways,
// or it goes on talking to the window it talked to meanwhile.
TEST(EngagementTest, GoingAwayAndComingBackIsToldBothWays)
{
    Engagement engagement;
    engagement.ContextCreated();
    engagement.Want(true);
    EXPECT_EQ(engagement.Want(false), Action::Leave);
    EXPECT_EQ(engagement.Want(true), Action::Enter);
}

TEST(EngagementTest, FocusTheInputMethodTookIsToldAgainWhenWanted)
{
    Engagement engagement;
    engagement.ContextCreated();
    engagement.Want(true);

    engagement.Removed();
    EXPECT_FALSE(engagement.IsEngaged());
    EXPECT_TRUE(engagement.Wants());
    EXPECT_EQ(engagement.Want(true), Action::Enter);
}
