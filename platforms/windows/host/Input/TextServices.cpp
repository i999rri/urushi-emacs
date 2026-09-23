#include "pch.h"
#include "Input/TextServices.h"

#include <textstor.h>

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
                                            ITfTextEditSink, ITfEditSession>
    {
        core::input::Keyboard* keyboard{ nullptr };
        HWND window{ nullptr };
        RECT caret{ 0, 0, 2, 16 };
        com_ptr<ITfContext> context;
        TfClientId client{ TF_CLIENTID_NULL };
        // How many compositions the input method has open, which is how
        // it says whether what it is doing has finished.
        int compositions{ 0 };
        // How long what was being composed was when it was last read,
        // which is what the next reading replaces.
        int32_t composed{ 0 };
        // A reading was asked for and has still to happen.
        bool pending{ false };
        TextServices::Logger log;

        void Say(std::string const& line) const
        {
            if (log)
            {
                log(line);
            }
        }

        // What the document holds now: what the input method has
        // settled on, at the start of it, and what it is still turning
        // over after that.
        //
        // Worked out from the document each time rather than remembered,
        // so that it is right however the edits and the compositions
        // fell out. GUID_PROP_COMPOSING says of each stretch which of
        // the two it is. What is settled is taken from the start alone:
        // text between two compositions cannot be un-settled once it has
        // gone to Emacs, so it is left where it is until the composition
        // before it has gone.
        void Read(TfEditCookie cookie, std::wstring& settled, std::wstring& composing)
        {
            com_ptr<ITfRange> whole;
            com_ptr<ITfReadOnlyProperty> properties;
            com_ptr<IEnumTfRanges> ranges;
            GUID const* wanted[] = { &GUID_PROP_COMPOSING };
            LONG shifted = 0;

            if (!context
                || FAILED(context->GetStart(cookie, whole.put()))
                || FAILED(whole->ShiftEnd(cookie, LONG_MAX, &shifted, nullptr))
                || FAILED(context->TrackProperties(wanted, ARRAYSIZE(wanted), nullptr, 0,
                                                   properties.put()))
                || FAILED(properties->EnumRanges(cookie, ranges.put(), whole.get())))
            {
                return;
            }

            bool composingFound = false;
            for (;;)
            {
                com_ptr<ITfRange> range;
                ULONG got = 0;

                if (FAILED(ranges->Next(1, range.put(), &got)) || got == 0)
                {
                    break;
                }

                composingFound = composingFound
                    || IsComposing(properties.get(), range.get(), cookie);
                (composingFound ? composing : settled) += ReadRange(range.get(), cookie);
            }
        }

        // Whether the input method is still turning RANGE over.
        static bool IsComposing(ITfReadOnlyProperty* properties, ITfRange* range,
                                TfEditCookie cookie)
        {
            VARIANT value{};
            com_ptr<IEnumTfPropertyValue> each;
            bool composing = false;

            if (FAILED(properties->GetValue(cookie, range, &value)))
            {
                return false;
            }
            if (value.vt == VT_UNKNOWN && value.punkVal
                && SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(each.put()))))
            {
                TF_PROPERTYVAL one{};

                while (each->Next(1, &one, nullptr) == S_OK)
                {
                    if (IsEqualGUID(one.guidId, GUID_PROP_COMPOSING))
                    {
                        composing = one.varValue.vt == VT_I4 && one.varValue.lVal != 0;
                    }
                    VariantClear(&one.varValue);
                }
            }
            VariantClear(&value);
            return composing;
        }

        // Give the keyboard what the document came to: what was settled
        // on goes to Emacs as typed, and what is still being turned over
        // is drawn at the cursor.
        void Deliver(std::wstring const& settled, std::wstring const& composing)
        {
            if (!settled.empty())
            {
                if (!keyboard->Composing())
                {
                    keyboard->CompositionStarted();
                }
                keyboard->TextUpdating(0, composed, settled);
                composed = 0;
                keyboard->CompositionCompleted();
            }

            if (composing.empty())
            {
                if (keyboard->Composing())
                {
                    keyboard->TextUpdating(0, composed, std::wstring{});
                    composed = 0;
                }
                return;
            }

            if (!keyboard->Composing())
            {
                keyboard->CompositionStarted();
            }
            keyboard->TextUpdating(0, composed, composing);
            composed = static_cast<int32_t>(composing.size());
        }

        // Take what was settled on out of the document.
        //
        // It has gone to Emacs, and a document that kept it would offer
        // it again with everything composed after it: what is settled
        // is read from the start, and the start is where it stays.
        void Erase(TfEditCookie cookie, size_t settled)
        {
            com_ptr<ITfRange> range;
            LONG shifted = 0;

            if (settled == 0 || !context
                || FAILED(context->GetStart(cookie, range.put()))
                || FAILED(range->ShiftEnd(cookie, static_cast<LONG>(settled), &shifted, nullptr)))
            {
                return;
            }
            range->SetText(cookie, 0, nullptr, 0);
        }

        // Ask to read the document.
        //
        // Asked for rather than read: this runs inside an edit session
        // of the input method's, and a session that may write cannot be
        // taken inside one that may not. One waiting is enough, since
        // the reading takes the document as it finds it.
        void AskToRead()
        {
            HRESULT session = S_OK;

            if (!context || pending)
            {
                return;
            }

            pending = true;
            if (FAILED(context->RequestEditSession(client, this,
                                                   TF_ES_READWRITE | TF_ES_ASYNC, &session))
                || FAILED(session))
            {
                pending = false;
            }
        }

        // Read it now, before a key goes to Emacs.
        //
        // The reading waits its turn while the key does not: the return
        // that settled a word would reach Emacs before the word did.
        void ReadNow()
        {
            HRESULT session = S_OK;

            if (!context || compositions > 0 || !pending)
            {
                return;
            }

            // The one waiting is left to run as well; it takes the
            // document as it is by then, and finds nothing more to say.
            context->RequestEditSession(client, this, TF_ES_READWRITE | TF_ES_SYNC, &session);
        }

        // ----- ITfEditSession -----

        STDMETHODIMP DoEditSession(TfEditCookie cookie) noexcept override
        {
            std::wstring settled;
            std::wstring composing;

            pending = false;
            if (!keyboard || !context)
            {
                return S_OK;
            }
            Read(cookie, settled, composing);
            Erase(cookie, settled.size());
            Deliver(settled, composing);
            return S_OK;
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

        STDMETHODIMP OnStartComposition(ITfCompositionView*, BOOL* ok) noexcept override
        {
            *ok = TRUE;
            ++compositions;
            Say("composition started, " + std::to_string(compositions) + " open");
            return S_OK;
        }

        STDMETHODIMP OnUpdateComposition(ITfCompositionView*, ITfRange*) noexcept override
        {
            return S_OK;
        }

        STDMETHODIMP OnEndComposition(ITfCompositionView*) noexcept override
        {
            if (compositions <= 0)
            {
                return E_FAIL;
            }
            if (--compositions == 0)
            {
                AskToRead();
            }
            Say("composition ending, " + std::to_string(compositions) + " open");
            return S_OK;
        }

        // ----- ITfTextEditSink -----

        STDMETHODIMP OnEndEdit(ITfContext*, TfEditCookie, ITfEditRecord*) noexcept override
        {
            if (compositions == 1)
            {
                AskToRead();
            }
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
        owner->client = m_client;

        com_ptr<ITfContext> context;
        TfEditCookie made = TF_INVALID_COOKIE;
        if (FAILED(m_document->CreateContext(
                m_client, 0, static_cast<ITfContextOwnerCompositionSink*>(owner.get()),
                context.put(), &made)))
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

    // Give the text services back what was taken from them.
    //
    // What they may still hold is let go of rather than left pointing
    // at a window that has gone: an edit session asked for and not yet
    // run holds the owner, and the owner is no use without the keyboard
    // it was made for.
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
                m_document->Pop(TF_POPF_ALL);
            }
            m_threads->Deactivate();
        }

        if (m_owner)
        {
            m_owner->keyboard = nullptr;
            m_owner->context = nullptr;
            m_owner->log = nullptr;
        }
        m_owner = nullptr;
        m_document = nullptr;
        m_threads = nullptr;
    }

    void TextServices::FlushComposition()
    {
        if (m_owner)
        {
            m_owner->ReadNow();
        }
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
            if (m_log)
            {
                m_log("no text services: the keys arrive as the letters on them");
            }
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
