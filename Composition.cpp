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

        element.Loaded([this](auto&&, auto&&) { Focus(true); });
    }

    void Composition::Focus(bool enter)
    {
        if (!m_context)
        {
            return;
        }

        if (enter)
        {
            m_context.NotifyFocusEnter();
        }
        else
        {
            m_context.NotifyFocusLeave();
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

    // Hand on what the input method settled on, and start afresh.
    void Composition::Settle()
    {
        if (!m_text.empty() && m_commit)
        {
            m_commit(m_text);
        }

        m_text.clear();
        if (m_composing_changed)
        {
            m_composing_changed(m_text);
        }
    }

    void Composition::Bind()
    {
        // What this context holds is the composition and nothing else:
        // the text Emacs has is Emacs's own, and telling the input
        // method about it would only invite it to change it.
        m_context.TextRequested([this](CoreTextEditContext const&,
                                       CoreTextTextRequestedEventArgs const& args) {
            auto request = args.Request();
            auto range = request.Range();
            auto size = static_cast<int32_t>(m_text.size());
            auto start = std::clamp(range.StartCaretPosition, 0, size);
            auto end = std::clamp(range.EndCaretPosition, start, size);

            // The range asked for is the range answered: it is not
            // ours to change.
            request.Text(hstring{ m_text.substr(static_cast<size_t>(start),
                                                static_cast<size_t>(end - start)) });
        });

        m_context.SelectionRequested([this](CoreTextEditContext const&,
                                            CoreTextSelectionRequestedEventArgs const& args) {
            auto caret = static_cast<int32_t>(m_text.size());

            args.Request().Selection({ caret, caret });
        });

        m_context.TextUpdating([this](CoreTextEditContext const&,
                                      CoreTextTextUpdatingEventArgs const& args) {
            auto range = args.Range();
            auto size = static_cast<int32_t>(m_text.size());
            auto start = std::clamp(range.StartCaretPosition, 0, size);
            auto end = std::clamp(range.EndCaretPosition, start, size);

            m_text.replace(static_cast<size_t>(start),
                           static_cast<size_t>(end - start),
                           std::wstring{ args.Text() });
            args.Result(CoreTextTextUpdatingResult::Succeeded);

            // Outside a composition there is nothing to turn over: what
            // arrives is what was meant, as when a key is typed with
            // the input method open but idle.
            if (m_composing)
            {
                if (m_composing_changed)
                {
                    m_composing_changed(m_text);
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
            m_composing = true;
        });

        m_context.CompositionCompleted([this](CoreTextEditContext const&,
                                              CoreTextCompositionCompletedEventArgs const&) {
            m_composing = false;
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
            m_composing = false;
            m_text.clear();
            if (m_composing_changed)
            {
                m_composing_changed(m_text);
            }
        });
    }
}
