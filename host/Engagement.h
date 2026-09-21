#pragma once

namespace urusi
{
    // What to tell the input method about whether the keys come here.
    //
    // Two things change on their own: whether the window wants the
    // input method (it has the focus, or has it back) and whether there
    // is a context to tell. Each call says the one thing to tell the
    // context now, and tells it nothing itself, so the rules can be
    // tested without an input method:
    //
    //   * wanting it before there is a context is remembered, and
    //     told once there is one
    //   * saying again what was said last is saying nothing: the input
    //     method ends what it is composing when told the focus has
    //     come while it thinks it never left
    //   * the input method taking the focus away on its own leaves the
    //     wish as it was, so that the next time it is wanted it is
    //     told again
    class Engagement
    {
    public:
        enum class Action { None, Enter, Leave };

        // The window's wish. What to tell the context now: nothing
        // while there is none, or when nothing changes.
        Action Want(bool engaged) noexcept
        {
            m_want = engaged;
            return Reconcile();
        }

        // There is a context now, to tell what was wished for.
        Action ContextCreated() noexcept
        {
            m_hasContext = true;
            return Reconcile();
        }

        // The input method took the focus away itself. There is nothing
        // to tell it: it knows.
        void Removed() noexcept { m_engaged = false; }

        bool HasContext() const noexcept { return m_hasContext; }
        bool Wants() const noexcept { return m_want; }
        bool IsEngaged() const noexcept { return m_engaged; }

    private:
        Action Reconcile() noexcept
        {
            if (!m_hasContext || m_want == m_engaged)
            {
                return Action::None;
            }

            m_engaged = m_want;
            return m_want ? Action::Enter : Action::Leave;
        }

        bool m_hasContext{ false };
        bool m_want{ false };
        bool m_engaged{ false };
    };
}
