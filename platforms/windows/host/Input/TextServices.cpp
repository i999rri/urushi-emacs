#include "pch.h"
#include "Input/TextServices.h"

#include <textstor.h>

#include "Text/Utf.h"

#pragma comment(lib, "ole32.lib")

using namespace winrt;

namespace
{
    // The text RANGE covers.
    std::wstring ReadRange(ITfRange* range, TfEditCookie cookie)
    {
        com_ptr<ITfRange> reading;

        if (FAILED(range->Clone(reading.put())))
        {
            return {};
        }

        std::wstring text;
        for (;;)
        {
            wchar_t buffer[128];
            ULONG got = 0;

            // MOVESTART, or the range would hand back the same text
            // again each time around.
            if (FAILED(reading->GetText(cookie, TF_TF_MOVESTART, buffer,
                                        ARRAYSIZE(buffer), &got))
                || got == 0)
            {
                break;
            }
            text.append(buffer, got);
        }
        return text;
    }

    // The window the input method is already talking to.
    //
    // It is not the window Windows knows this one by: what draws the
    // window puts the keyboard's focus in a window of its own inside
    // it, and the text services follow the focus rather than the frame.
    // Associating the frame leaves them talking to that inner window
    // still, and the keys arrive as the letters on them.
    //
    // Found by asking the text services where they are, rather than by
    // the name of the window class, which is theirs to change. OURS is
    // passed over: the answer would otherwise be the window we told
    // them about ourselves.
    HWND WindowOfTheActiveContext(ITfThreadMgrEx* threads, ITfDocumentMgr* ours)
    {
        com_ptr<IEnumTfDocumentMgrs> documents;

        if (FAILED(threads->EnumDocumentMgrs(documents.put())))
        {
            return nullptr;
        }

        for (;;)
        {
            com_ptr<ITfDocumentMgr> document;
            com_ptr<ITfContext> context;
            com_ptr<ITfContextView> view;
            ULONG got = 0;
            HWND window = nullptr;

            if (FAILED(documents->Next(1, document.put(), &got)) || got == 0)
            {
                return nullptr;
            }
            if (document.get() != ours
                && SUCCEEDED(document->GetTop(context.put())) && context
                && SUCCEEDED(context->GetActiveView(view.put()))
                && SUCCEEDED(view->GetWnd(&window)) && window)
            {
                return window;
            }
        }
    }
}

namespace urusi::windows::input
{
    // What the text services ask of the window, and what they tell it.
    struct TextServices::Owner : implements<Owner, ITfContextOwner,
                                            ITfContextOwnerCompositionSink,
                                            ITfTextEditSink>
    {
        core::input::Keyboard* keyboard{ nullptr };
        HWND window{ nullptr };
        RECT caret{ 0, 0, 2, 16 };
        com_ptr<ITfContext> context;
        // What is being composed, which is the only part of the
        // document that is this window's to read.
        com_ptr<ITfCompositionView> composition;
        int compositions{ 0 };
        // How long what was composed was when it was last read, which is
        // what the next reading replaces.
        int32_t composed{ 0 };
        // The composition ended while there was no way to read what it
        // ended with: the edit it ended in has still to be finished, and
        // what it says is the answer.
        bool ending{ false };
        TextServices::Logger log;

        void Say(std::string const& line) const
        {
            if (log)
            {
                log(line);
            }
        }

        // Hand on what is being composed.
        //
        // What the composition covers, and not what the document holds:
        // the document is not emptied of what a composition settled on,
        // and reading the whole of it would hand the text before this
        // one over again with it.
        void Take(TfEditCookie cookie)
        {
            com_ptr<ITfRange> range;

            if (!composition || FAILED(composition->GetRange(range.put())) || !range)
            {
                return;
            }

            std::wstring text = ReadRange(range.get(), cookie);

            Say("read \"" + core::text::ToUtf8(text) + "\" over " + std::to_string(composed));

            // A composition the text services have open is one the
            // keyboard is to be turning over rather than handing on.
            // Out of step, every reading would be handed to Emacs as
            // though it had been settled on.
            if (!keyboard->Composing())
            {
                keyboard->CompositionStarted();
            }
            keyboard->TextUpdating(0, composed, text);
            composed = static_cast<int32_t>(text.size());
        }

        // ----- ITfContextOwner -----

        STDMETHODIMP GetACPFromPoint(POINT const*, DWORD, LONG*) noexcept override
        {
            return E_NOTIMPL;
        }

        STDMETHODIMP GetTextExt(LONG, LONG, RECT* rect, BOOL* clipped) noexcept override
        {
            *rect = caret;
            *clipped = FALSE;
            return S_OK;
        }

        STDMETHODIMP GetScreenExt(RECT* rect) noexcept override
        {
            return GetWindowRect(window, rect) ? S_OK : E_FAIL;
        }

        STDMETHODIMP GetStatus(TF_STATUS* status) noexcept override
        {
            status->dwDynamicFlags = 0;
            // Transitory: what has been settled on is gone, and the
            // input method may not reach back for it. Nothing is
            // hidden either; the document holds only the composition.
            status->dwStaticFlags = TS_SS_TRANSITORY | TS_SS_NOHIDDENTEXT;
            return S_OK;
        }

        STDMETHODIMP GetWnd(HWND* out) noexcept override
        {
            *out = window;
            return S_OK;
        }

        STDMETHODIMP GetAttribute(REFGUID, VARIANT* value) noexcept override
        {
            // Nothing to say about any of them.
            VariantInit(value);
            return S_OK;
        }

        // ----- ITfContextOwnerCompositionSink -----

        STDMETHODIMP OnStartComposition(ITfCompositionView* view, BOOL* ok) noexcept override
        {
            *ok = TRUE;
            composition.copy_from(view);
            if (compositions++ == 0)
            {
                composed = 0;
                ending = false;
                keyboard->CompositionStarted();
            }
            Say("composition started, " + std::to_string(compositions) + " open");
            return S_OK;
        }

        STDMETHODIMP OnUpdateComposition(ITfCompositionView* view, ITfRange*) noexcept override
        {
            composition.copy_from(view);
            return S_OK;
        }

        STDMETHODIMP OnEndComposition(ITfCompositionView*) noexcept override
        {
            // Settled where there is no cookie to read with: what it
            // came to was read as it was composed, and is handed on when
            // the edit it ended in is finished.
            if (--compositions == 0)
            {
                ending = true;
            }
            Say("composition ending, " + std::to_string(compositions) + " open");
            return S_OK;
        }

        // ----- ITfTextEditSink -----

        STDMETHODIMP OnEndEdit(ITfContext*, TfEditCookie cookie, ITfEditRecord*) noexcept override
        {
            if (!ending)
            {
                Take(cookie);
                return S_OK;
            }

            ending = false;
            composed = 0;
            composition = nullptr;
            keyboard->CompositionCompleted();
            return S_OK;
        }
    };

    TextServices::TextServices() = default;

    TextServices::~TextServices()
    {
        Stop();
    }

    bool TextServices::Start(HWND frame, core::input::Keyboard& keyboard, Logger log)
    {
        m_frame = frame;
        m_keyboard = &keyboard;
        m_log = std::move(log);

        // There will be a document to give the keys to, which is what
        // the keyboard waits to hear before it says they come here.
        // Making it now would be making it for a window that has no
        // keys and has not been shown.
        keyboard.ContextCreated();
        return true;
    }

    // The document, made the first time the keys come here and kept
    // from then on.
    //
    // Not made when the window opens: what draws the window has its own
    // to make first, and what it makes is where the keys go. Made once
    // and never given back, as the text services would rather be left
    // alone than started and stopped.
    bool TextServices::Ensure()
    {
        if (m_document)
        {
            return true;
        }
        if (!m_keyboard)
        {
            return false;
        }

        auto owner = make_self<Owner>();

        owner->keyboard = m_keyboard;
        owner->log = m_log;
        owner->window = m_frame;

        if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(m_threads.put()))))
        {
            return false;
        }

        // The input processors are wanted: they are what turns the keys
        // into what was meant by them. A console asks for them to be
        // left alone; a window does not.
        if (FAILED(m_threads->ActivateEx(&m_client, 0))
            || FAILED(m_threads->CreateDocumentMgr(m_document.put())))
        {
            Stop();
            return false;
        }

        com_ptr<ITfContext> context;
        if (FAILED(m_document->CreateContext(
                m_client, 0, static_cast<ITfContextOwnerCompositionSink*>(owner.get()),
                context.put(), &m_editCookie)))
        {
            Stop();
            return false;
        }

        com_ptr<ITfSource> source;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(source.put())))
            || FAILED(source->AdviseSink(IID_ITfContextOwner,
                                         static_cast<ITfContextOwner*>(owner.get()),
                                         &m_ownerCookie))
            || FAILED(source->AdviseSink(IID_ITfTextEditSink,
                                         static_cast<ITfTextEditSink*>(owner.get()),
                                         &m_editSinkCookie))
            || FAILED(m_document->Push(context.get())))
        {
            Stop();
            return false;
        }

        owner->context = context;
        m_owner = owner;
        owner->Say("a document of our own");
        return true;
    }

    void TextServices::Stop()
    {
        if (m_owner && m_owner->context)
        {
            com_ptr<ITfSource> source;
            if (SUCCEEDED(m_owner->context->QueryInterface(IID_PPV_ARGS(source.put()))))
            {
                if (m_ownerCookie != TF_INVALID_COOKIE)
                {
                    source->UnadviseSink(m_ownerCookie);
                }
                if (m_editSinkCookie != TF_INVALID_COOKIE)
                {
                    source->UnadviseSink(m_editSinkCookie);
                }
            }
            m_ownerCookie = TF_INVALID_COOKIE;
            m_editSinkCookie = TF_INVALID_COOKIE;
        }

        if (m_threads)
        {
            if (m_document)
            {
                com_ptr<ITfContext> popped;
                m_document->Pop(TF_POPF_ALL);
            }
            m_threads->Deactivate();
        }

        m_owner = nullptr;
        m_document = nullptr;
        m_threads = nullptr;
        m_frame = nullptr;
    }

    bool TextServices::TakesKey(core::input::KeyEvent const& key)
    {
        if (!m_threads || key.key == 0)
        {
            return false;
        }

        auto keys = m_threads.try_as<ITfKeystrokeMgr>();
        if (!keys)
        {
            return false;
        }

        // The lParam Windows would have sent, which is where the text
        // services read the scan code and the rest of it.
        WPARAM wParam = static_cast<WPARAM>(key.key);
        LPARAM lParam = static_cast<LPARAM>(key.repeat & 0xFFFF)
            | (static_cast<LPARAM>(key.scanCode & 0xFF) << 16)
            | (key.extended ? (LPARAM{ 1 } << 24) : 0)
            | (key.menuDown ? (LPARAM{ 1 } << 29) : 0)
            | (key.wasDown ? (LPARAM{ 1 } << 30) : 0)
            | (key.down ? 0 : (LPARAM{ 1 } << 31));

        // Asked first, and only given the key if it says it wants it:
        // a key handed over that it turns down is a key it has seen
        // twice.
        BOOL wanted = FALSE;
        HRESULT asked = key.down ? keys->TestKeyDown(wParam, lParam, &wanted)
                                 : keys->TestKeyUp(wParam, lParam, &wanted);
        if (FAILED(asked) || !wanted)
        {
            return false;
        }

        BOOL taken = FALSE;
        HRESULT given = key.down ? keys->KeyDown(wParam, lParam, &taken)
                                 : keys->KeyUp(wParam, lParam, &taken);
        return SUCCEEDED(given) && taken;
    }

    // Which window the keys go to, which the text services ask for
    // rather than being told.
    //
    // It is not the window Windows knows this one by: what draws the
    // window makes an inner window to hold the focus, and makes it when
    // it wants one rather than when the window opens. Answering with
    // the frame leaves the text services talking to that inner window,
    // and the keys arrive as the letters on them.
    void TextServices::FindTheKeys()
    {
        HWND keys = GetFocus();

        if (!keys)
        {
            keys = WindowOfTheActiveContext(m_threads.get(), m_document.get());
        }
        if (!keys || keys == m_owner->window)
        {
            return;
        }

        m_owner->window = keys;
        m_owner->Say("the keys are in window " + std::to_string(reinterpret_cast<INT_PTR>(keys)));
    }

    void TextServices::NotifyFocusEnter()
    {
        if (!Ensure())
        {
            return;
        }

        FindTheKeys();

        HRESULT done = m_threads->SetFocus(m_document.get());

        m_owner->Say("keys come here (" + std::to_string(done) + ")");
    }

    void TextServices::NotifyFocusLeave()
    {
        if (!m_threads)
        {
            return;
        }

        // What it was turning over goes now, while there is still
        // somewhere for it to go: told the keys have gone, an input
        // method holds on to it and hands it over to whatever has them
        // next.
        if (m_owner && m_owner->context)
        {
            com_ptr<ITfContextOwnerCompositionServices> compositions;
            if (SUCCEEDED(m_owner->context->QueryInterface(IID_PPV_ARGS(compositions.put()))))
            {
                compositions->TerminateComposition(nullptr);
            }
        }
        m_threads->SetFocus(nullptr);
        m_owner->Say("the keys have gone");
    }

    // Have the text services settle what they are turning over.
    //
    // Theirs to settle, not the window's: they keep the composition,
    // and one ended behind their back leaves them adding to a document
    // the window has already handed on.
    void TextServices::EndComposition()
    {
        if (!m_owner || !m_owner->context)
        {
            return;
        }

        com_ptr<ITfContextOwnerCompositionServices> compositions;
        if (SUCCEEDED(m_owner->context->QueryInterface(IID_PPV_ARGS(compositions.put()))))
        {
            compositions->TerminateComposition(nullptr);
        }
    }

    void TextServices::SetCaret(RECT const& caret)
    {
        if (m_owner)
        {
            m_owner->caret = caret;
        }
    }
}
