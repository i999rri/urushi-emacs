#include "pch.h"
#include "Composition.h"

#include <algorithm>

using namespace winrt;
using namespace Windows::UI::Text::Core;

namespace urusi
{
    void Composition::Start(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                            CommitFn commit, ComposingFn composing)
    {
        m_commit = std::move(commit);
        m_composing_changed = std::move(composing);

        auto manager = CoreTextServicesManager::GetForCurrentView();
        m_context = manager.CreateEditContext();

        // The window draws its own text and its own caret, and asks
        // for the pane itself when it wants one.
        m_context.InputPaneDisplayPolicy(CoreTextInputPaneDisplayPolicy::Manual);
        m_context.InputScope(CoreTextInputScope::Default);
        Bind();
        Perform(m_engagement.ContextCreated());

        // The input method only talks to a context that has the focus,
        // and turning it on and off is part of that talk, from a key or
        // from the taskbar alike. The focus the element has is the
        // focus the context has.
        element.Loaded([this](auto&&, auto&&) { Focus(true); });
        element.GotFocus([this](auto&&, auto&&) { Focus(true); });
        element.LostFocus([this](auto&&, auto&&) { Focus(false); });
    }

    void Composition::Focus(bool enter)
    {
        Perform(m_engagement.Want(enter));
    }

    void Composition::Perform(Engagement::Action action)
    {
        switch (action)
        {
        case Engagement::Action::Enter:
            // The input method counts from nothing again once told.
            Forget();
            m_context.NotifyFocusEnter();
            Say("focus enter");
            break;
        case Engagement::Action::Leave:
            m_context.NotifyFocusLeave();
            Say("focus leave");
            break;
        case Engagement::Action::None:
            break;
        }
    }

    void Composition::SetCaret(winrt::Windows::Foundation::Rect const& caret)
    {
        m_caret = caret;

        if (m_context)
        {
            m_context.NotifyLayoutChanged();
        }
    }

    void Composition::Say(std::string const& what)
    {
        constexpr int kLimit = 400;

        if (m_trace && m_said < kLimit)
        {
            ++m_said;
            m_trace(what + "\n");
        }
    }

    // Hand on what the input method settled on. The input method is
    // told nothing: it counts on from where this ends, and so does the
    // buffer.
    void Composition::Settle()
    {
        auto settled = m_buffer.Settle();

        Say("settle " + to_string(hstring{ settled }));
        if (!settled.empty() && m_commit)
        {
            m_commit(settled);
        }
        if (m_composing_changed)
        {
            m_composing_changed(std::wstring{});
        }
    }

    // Drop what is being composed and start counting from nothing, as
    // the input method does when the focus comes or is taken away.
    void Composition::Forget()
    {
        bool drawn = m_buffer.Composing() || !m_buffer.Composed().empty();

        m_buffer.Reset();
        if (drawn && m_composing_changed)
        {
            m_composing_changed(std::wstring{});
        }
    }

    void Composition::Bind()
    {
        // What this context holds is the composition and nothing else:
        // the text Emacs has is Emacs's own, and telling the input
        // method about it would only invite it to change it. What was
        // handed on is answered with spaces, where the input method
        // counts it to be.
        m_context.TextRequested([this](CoreTextEditContext const&,
                                       CoreTextTextRequestedEventArgs const& args) {
            auto request = args.Request();
            auto range = request.Range();

            request.Text(hstring{ m_buffer.Text(range.StartCaretPosition,
                                                range.EndCaretPosition) });
            Say("requested " + std::to_string(range.StartCaretPosition) + ".."
                + std::to_string(range.EndCaretPosition));
        });

        m_context.SelectionRequested([this](CoreTextEditContext const&,
                                            CoreTextSelectionRequestedEventArgs const& args) {
            auto caret = m_buffer.Caret();

            args.Request().Selection({ caret, caret });
        });

        m_context.TextUpdating([this](CoreTextEditContext const&,
                                      CoreTextTextUpdatingEventArgs const& args) {
            auto range = args.Range();

            m_buffer.Update(range.StartCaretPosition, range.EndCaretPosition,
                            std::wstring{ args.Text() });
            args.Result(CoreTextTextUpdatingResult::Succeeded);
            Say("updating " + std::to_string(range.StartCaretPosition) + ".."
                + std::to_string(range.EndCaretPosition) + " \""
                + to_string(args.Text()) + "\" -> \""
                + to_string(hstring{ m_buffer.Composed() })
                + "\", composing " + (m_buffer.Composing() ? "yes" : "no"));

            // Outside a composition there is nothing to turn over: what
            // arrives is what was meant, as when a key is typed with
            // the input method open but idle.
            if (m_buffer.Composing())
            {
                if (m_composing_changed)
                {
                    m_composing_changed(m_buffer.Composed());
                }
            }
            else
            {
                Settle();
            }
        });

        m_context.SelectionUpdating([](CoreTextEditContext const&,
                                       CoreTextSelectionUpdatingEventArgs const& args) {
            // The caret is always at the end of the composition here.
            args.Result(CoreTextSelectionUpdatingResult::Succeeded);
        });

        m_context.CompositionStarted([this](CoreTextEditContext const&,
                                            CoreTextCompositionStartedEventArgs const&) {
            m_buffer.Started();
            Say("started");
        });

        m_context.CompositionCompleted([this](CoreTextEditContext const&,
                                              CoreTextCompositionCompletedEventArgs const&) {
            Say("completed");
            Settle();
        });

        // Where to put the candidates: beside the caret, as anywhere
        // else would be.
        m_context.LayoutRequested([this](CoreTextEditContext const&,
                                         CoreTextLayoutRequestedEventArgs const& args) {
            args.Request().LayoutBounds().ControlBounds(m_caret);
            args.Request().LayoutBounds().TextBounds(m_caret);
        });

        m_context.FocusRemoved([this](CoreTextEditContext const&, auto&&) {
            // Taken away by the input method, not by us: the next focus
            // there is to be told of again, and it starts counting from
            // nothing then. What was half composed is dropped rather
            // than left drawn.
            m_engagement.Removed();
            Say("focus removed");
            Forget();
        });
    }
}
