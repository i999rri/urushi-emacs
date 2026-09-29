#pragma once

#include <msctf.h>

#include <winrt/base.h>

#include <functional>
#include <string>

#include "Input/Keyboard.h"

namespace urushi::windows::input
{
    // The input method's side of the window, spoken to as Windows's text
    // services want to be spoken to.
    //
    // An input method does not send characters; it holds a conversation.
    // It asks where the text is on the screen, puts what it is turning
    // over into a document, and says when it has settled on something.
    // This is the window's side of that document.
    //
    // The document is transitory: it holds what is being composed and
    // nothing else, and the input method is told it cannot go back to
    // what it has already settled on. Emacs has been given that and put
    // it in a buffer, where the input method cannot follow, so this is
    // the only kind of document that tells it the truth. WinRT's
    // CoreTextEditContext cannot make one, which is why this talks to
    // the text services themselves.
    //
    // It is the device the keys come from, to the Keyboard: what to make
    // of the conversation is the Keyboard's to decide, and this passes
    // on what the input method says and tells it what the Keyboard asks.
    //
    // UI thread only.
    class TextServices : public core::input::IKeyInputDevice
    {
    public:
        // One line for each thing the input method says, for the log.
        using Logger = std::function<void(std::string const&)>;

        // Made and given back where what it holds is a whole type,
        // which is in TextServices.cpp and nowhere else.
        TextServices();
        ~TextServices();
        TextServices(TextServices const&) = delete;
        TextServices& operator=(TextServices const&) = delete;

        // WINDOW is the window whose keys the input method is to have.
        // KEYBOARD hears everything it says, and outlives this. False if
        // the text services would not have it, and the window is left
        // with the keys as the letters on them.
        bool Start(HWND window, core::input::Keyboard& keyboard, Logger log);
        void Stop();

        bool Started() const noexcept { return m_document != nullptr; }

        // Offer KEY to the input method, and say whether it took it.
        //
        // A key they take is theirs: it is what turns them on and off
        // and works a conversion through, and Emacs is not to see it.
        bool TakesKey(core::input::KeyEvent const& key);

        // Read what the input method has settled on, if it has not been
        // read yet, before a key goes to Emacs: the reading waits its
        // turn while the key does not, and the return that settled a
        // word would reach Emacs before the word did.
        void FlushComposition();

        // Settle what is being composed, as turning the input method
        // off is to.
        void EndComposition();

        // IKeyInputDevice
        void NotifyFocusEnter() override;
        void NotifyFocusLeave() override;

        // Where the caret is, in the pixels of the screen, and how wide
        // a character is drawn there: that is where the candidates are
        // shown, and how far along a stretch of the composition is.
        void SetCaret(RECT const& caret, long advance);

    private:
        struct Owner;

        bool Ensure();
        void FindTheKeys();

        winrt::com_ptr<Owner> m_owner;
        winrt::com_ptr<ITfThreadMgrEx> m_threads;
        winrt::com_ptr<ITfDocumentMgr> m_document;
        TfClientId m_client{ TF_CLIENTID_NULL };
        // What each sink was taken on with, which is what gives it back.
        DWORD m_ownerCookie{ TF_INVALID_COOKIE };
        DWORD m_editSinkCookie{ TF_INVALID_COOKIE };
        // The window Windows knows this one by, which is where the
        // search for the one the keys go to begins and ends.
        HWND m_frame{ nullptr };
        core::input::Keyboard* m_keyboard{ nullptr };
        Logger m_log;
    };
}
