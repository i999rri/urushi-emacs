#include <gtest/gtest.h>

#include "Splitter.h"

#include <vector>

using urusi::ParseSplitter;
using urusi::PointerButton;
using urusi::PointerEvent;
using urusi::PointerKind;
using urusi::Splitter;

namespace
{
    // A pointer that says what was asked of it.
    struct Pointer : urusi::IPointerDevice
    {
        void Capture() override { captured = true; }
        void Release() override { captured = false; }
        bool captured{ false };
    };

    // The parts either side of a splitter, as a grid would have them.
    struct Tracks : urusi::ISplitterTracks
    {
        double Length(bool before) const override { return before ? before_ : after_; }
        bool Shares(bool before) const override { return before ? beforeShares : afterShares; }

        // The grid gives a part its size, and the one that shares takes
        // what is left, as a Grid lays them out.
        void SetLength(bool before, double length) override
        {
            double total = before_ + after_;
            if (before)
            {
                before_ = length;
                after_ = total - length;
                beforeShares = false;
            }
            else
            {
                after_ = length;
                before_ = total - length;
                afterShares = false;
            }
        }

        double before_{ 500 };
        double after_{ 200 };
        bool beforeShares{ true };
        bool afterShares{ false };
    };

    struct Drag
    {
        Pointer pointer;
        Tracks tracks;
        int started{ 0 };
        std::vector<std::pair<double, double>> finished;
        Splitter splitter;

        explicit Drag(bool horizontal = false)
            : splitter({ .horizontal = horizontal, .before = L"editor", .after = L"output" },
                       pointer, tracks,
                       { .started = [this] { ++started; },
                         .finished = [this](double b, double a) { finished.emplace_back(b, a); } })
        {
        }

        bool Press(double at)
        {
            return splitter.Handle({ .kind = PointerKind::Pressed, .x = at, .y = at,
                                     .button = PointerButton::Left, .left = true });
        }
        bool Move(double at)
        {
            return splitter.Handle({ .kind = PointerKind::Moved, .x = at, .y = at, .left = true });
        }
        bool Release(double at)
        {
            return splitter.Handle({ .kind = PointerKind::Released, .x = at, .y = at,
                                     .button = PointerButton::Left });
        }
    };
}

TEST(SplitterTest, NameSaysTheWayAndThePartsEitherSide)
{
    auto side = ParseSplitter(L"urusi-splitter:h:explorer:editor");
    ASSERT_TRUE(side);
    EXPECT_TRUE(side->horizontal);
    EXPECT_EQ(side->before, L"explorer");
    EXPECT_EQ(side->after, L"editor");

    auto stacked = ParseSplitter(L"urusi-splitter:v:editor:output");
    ASSERT_TRUE(stacked);
    EXPECT_FALSE(stacked->horizontal);
}

TEST(SplitterTest, OtherNamesAreNotSplitters)
{
    EXPECT_FALSE(ParseSplitter(L"urusi-frame"));
    EXPECT_FALSE(ParseSplitter(L"urusi-splitter:"));
    EXPECT_FALSE(ParseSplitter(L"urusi-splitter:x:a:b"));
}

// The editor shares what is left and the output panel has a size of its
// own: dragging up makes the output taller, and the editor takes the
// rest. Let go, it says where it ended.
TEST(SplitterTest, ADragResizesThePartWithASizeOfItsOwn)
{
    Drag drag;
    EXPECT_TRUE(drag.Press(500));
    EXPECT_TRUE(drag.pointer.captured);
    EXPECT_EQ(drag.started, 1);
    EXPECT_TRUE(drag.splitter.Dragging());

    EXPECT_TRUE(drag.Move(450));
    EXPECT_EQ(drag.tracks.after_, 250);
    EXPECT_EQ(drag.tracks.before_, 450);
    EXPECT_TRUE(drag.tracks.beforeShares);

    EXPECT_TRUE(drag.Release(450));
    EXPECT_FALSE(drag.pointer.captured);
    EXPECT_FALSE(drag.splitter.Dragging());
    ASSERT_EQ(drag.finished.size(), 1u);
    EXPECT_EQ(drag.finished[0], std::make_pair(450.0, 250.0));
}

TEST(SplitterTest, NeitherWithASizeGivesTheOneBeforeOne)
{
    Drag drag{ true };
    drag.tracks.afterShares = true;
    drag.Press(100);
    drag.Move(120);
    EXPECT_EQ(drag.tracks.before_, 520);
    EXPECT_FALSE(drag.tracks.beforeShares);
}

TEST(SplitterTest, NoPartIsMadeShorterThanTheLeast)
{
    Drag drag;
    drag.Press(500);
    drag.Move(2000);
    EXPECT_EQ(drag.tracks.after_, Splitter::kLeast);
    drag.Move(-2000);
    EXPECT_EQ(drag.tracks.before_, Splitter::kLeast);
}

// Both already shorter than the least between them: nothing moves.
TEST(SplitterTest, PartsWithNoRoomDoNotMove)
{
    Drag drag;
    drag.tracks.before_ = 30;
    drag.tracks.after_ = 30;
    drag.Press(30);
    drag.Move(40);
    EXPECT_EQ(drag.tracks.before_, 30);
    EXPECT_EQ(drag.tracks.after_, 30);
}

// Moving over it without a press, or letting go of nothing, is not the
// splitter's: it is left to whatever is under it.
TEST(SplitterTest, OnlyADragIsTheSplitters)
{
    Drag drag;
    EXPECT_FALSE(drag.Move(10));
    EXPECT_FALSE(drag.Release(10));
    EXPECT_FALSE(drag.splitter.Handle({ .kind = PointerKind::Pressed,
                                        .button = PointerButton::Right, .right = true }));
    EXPECT_TRUE(drag.finished.empty());
}

// The pointer taken away mid-drag, as by a window coming up: the drag
// ends there, and says so.
TEST(SplitterTest, LosingThePointerEndsTheDrag)
{
    Drag drag;
    drag.Press(500);
    drag.Move(480);
    EXPECT_TRUE(drag.splitter.Handle({ .kind = PointerKind::CaptureLost }));
    EXPECT_FALSE(drag.splitter.Dragging());
    EXPECT_EQ(drag.finished.size(), 1u);
}
