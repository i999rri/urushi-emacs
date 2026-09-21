#include "pch.h"
#include "Composition.h"

using namespace winrt;
using namespace Windows::UI::Text::Core;

namespace urusi
{
    void Composition::Start(winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                            Keyboard& keyboard)
    {
        m_keyboard = &keyboard;

        auto manager = CoreTextServicesManager::GetForCurrentView();
        m_context = manager.CreateEditContext();

        // The window draws its own text and its own caret, and asks
        // for the pane itself when it wants one.
        m_context.InputPaneDisplayPolicy(CoreTextInputPaneDisplayPolicy::Manual);
        m_context.InputScope(CoreTextInputScope::Default);
        Bind();
        m_keyboard->ContextCreated();

        // The input method only talks to a context that has the focus,
        // and turning it on and off is part of that talk, from a key or
        // from the taskbar alike. The focus the element has is the
        // focus the context has.
        element.Loaded([this](auto&&, auto&&) { m_keyboard->FocusGained(); });
        element.GotFocus([this](auto&&, auto&&) { m_keyboard->FocusGained(); });
        element.LostFocus([this](auto&&, auto&&) { m_keyboard->FocusLost(); });
    }

    void Composition::NotifyFocusEnter()
    {
        if (m_context)
        {
            m_context.NotifyFocusEnter();
        }
    }

    void Composition::NotifyFocusLeave()
    {
        if (m_context)
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

    void Composition::Bind()
    {
        // What this context holds is the composition and nothing else:
        // the text Emacs has is Emacs's own, and telling the input
        // method about it would only invite it to change it.
        m_context.TextRequested([this](CoreTextEditContext const&,
                                       CoreTextTextRequestedEventArgs const& args) {
            auto request = args.Request();
            auto range = request.Range();

            request.Text(hstring{ m_keyboard->TextRequested(range.StartCaretPosition,
                                                            range.EndCaretPosition) });
        });

        m_context.SelectionRequested([this](CoreTextEditContext const&,
                                            CoreTextSelectionRequestedEventArgs const& args) {
            auto caret = m_keyboard->SelectionRequested();

            args.Request().Selection({ caret, caret });
        });

        m_context.TextUpdating([this](CoreTextEditContext const&,
                                      CoreTextTextUpdatingEventArgs const& args) {
            auto range = args.Range();

            args.Result(CoreTextTextUpdatingResult::Succeeded);
            m_keyboard->TextUpdating(range.StartCaretPosition, range.EndCaretPosition,
                                     std::wstring{ args.Text() });
        });

        m_context.SelectionUpdating([](CoreTextEditContext const&,
                                       CoreTextSelectionUpdatingEventArgs const& args) {
            // The caret is always at the end of the composition here.
            args.Result(CoreTextSelectionUpdatingResult::Succeeded);
        });

        m_context.CompositionStarted([this](CoreTextEditContext const&,
                                            CoreTextCompositionStartedEventArgs const&) {
            m_keyboard->CompositionStarted();
        });

        m_context.CompositionCompleted([this](CoreTextEditContext const&,
                                              CoreTextCompositionCompletedEventArgs const&) {
            m_keyboard->CompositionCompleted();
        });

        // Where to put the candidates: beside the caret, as anywhere
        // else would be.
        m_context.LayoutRequested([this](CoreTextEditContext const&,
                                         CoreTextLayoutRequestedEventArgs const& args) {
            args.Request().LayoutBounds().ControlBounds(m_caret);
            args.Request().LayoutBounds().TextBounds(m_caret);
        });

        m_context.FocusRemoved([this](CoreTextEditContext const&, auto&&) {
            m_keyboard->FocusRemoved();
        });
    }
}
