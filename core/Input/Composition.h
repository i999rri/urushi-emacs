#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace urusi::core::input
{
    // How a stretch of what is being composed is marked.
    //
    // The line under it is how an input method says which part of the
    // text it is working on: a thick or wavy line under the clause being
    // converted now, a thin one under the rest of it. Which line means
    // what is the input method's to decide, and the window only draws
    // what it is told.
    enum class Underline
    {
        None,
        Solid,
        Dotted,
        Dashed,
        Wavy,
        Double,
    };

    // One stretch of what is being composed, marked the same way
    // throughout. LENGTH counts the characters of the text it covers,
    // from where the stretch before it ended.
    struct CompositionRun
    {
        size_t length{ 0 };
        Underline underline{ Underline::None };
        // Empty unless the input method asked for both of them: one
        // without the other is an input method that has not been tried
        // against a window of this colour.
        std::string foreground;
        std::string background;
    };

    // What the input method is turning over: the text of it, how its
    // stretches are marked, and how far into it the caret is.
    struct Composition
    {
        std::wstring text;
        std::vector<CompositionRun> runs;
        size_t caret{ 0 };
    };
}
