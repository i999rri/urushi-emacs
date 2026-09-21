#pragma once

namespace urusi::core::input
{
    // What to do as the window comes to the front and goes behind.
    //
    // XAML says the window has gone when it has not, as a window of the
    // input method comes and goes, and says it is active while it
    // already is. So a going is only believed once Windows agrees, when
    // what XAML is doing about it has been done; and a coming back is
    // only told on to the input method once XAML has put the focus
    // back. Each call says what to do and does none of it, so that the
    // order of what happens can be tested without a window:
    //
    //   Activated           the window says it is in front
    //   Deactivated         it says it has gone: check later
    //   DeactivationChecked Windows was asked whether it has
    //   ResumeChecked       the coming back, once XAML is done with it
    class Activation
    {
    public:
        struct Actions
        {
            // Look again later whether the window is still in front.
            bool checkLater{ false };

            // Tell the input method, once XAML has put the focus back,
            // that the keys come here again.
            bool resumeLater{ false };

            // Tell the input method the keys come here, or have gone.
            bool enterInputMethod{ false };
            bool leaveInputMethod{ false };

            // Tell Emacs its frame has the focus, or has lost it.
            bool tellEmacsFocused{ false };
            bool tellEmacsUnfocused{ false };
        };

        Actions Activated() noexcept
        {
            Actions actions;

            // Only on coming back: telling the input method again while
            // it never went ends what it is composing.
            actions.resumeLater = !m_active;
            actions.tellEmacsFocused = true;
            m_active = true;
            return actions;
        }

        Actions Deactivated() noexcept
        {
            Actions actions;
            actions.checkLater = true;
            return actions;
        }

        Actions DeactivationChecked(bool foreground) noexcept
        {
            Actions actions;

            // Still in front: it was not gone, and nothing changes.
            if (foreground || !m_active)
            {
                return actions;
            }

            // Told it has gone, so that coming back is news to it.
            actions.leaveInputMethod = true;
            actions.tellEmacsUnfocused = true;
            m_active = false;
            return actions;
        }

        Actions ResumeChecked(bool foreground, bool keysComeHere) noexcept
        {
            Actions actions;
            actions.enterInputMethod = foreground && keysComeHere;
            return actions;
        }

        bool Active() const noexcept { return m_active; }

    private:
        bool m_active{ false };
    };
}
