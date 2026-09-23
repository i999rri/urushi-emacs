#include "pch.h"
#include "Input/TextServices.h"

#include <textstor.h>

#pragma comment(lib, "ole32.lib")

using namespace winrt;

namespace
{
    // The whole of what the document holds, which with a transitory
    // document is what is being composed and nothing else.
    std::wstring ReadDocument(ITfContext* context, TfEditCookie cookie)
    {
        com_ptr<ITfRange> range;
        com_ptr<ITfRange> end;

        if (FAILED(context->GetStart(cookie, range.put()))
            || FAILED(context->GetEnd(cookie, end.put()))
            || FAILED(range->ShiftEndToRange(cookie, end.get(), TF_ANCHOR_END)))
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
            if (FAILED(range->GetText(cookie, TF_TF_MOVESTART, buffer,
                                      ARRAYSIZE(buffer), &got))
                || got == 0)
            {
                break;
            }
            text.append(buffer, got);
        }
        return text;
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

        // Hand on what the document holds now, whether it is being
        // composed or was typed with the input method open and idle.
        void Take(TfEditCookie cookie)
        {
            std::wstring text = context ? ReadDocument(context.get(), cookie) : std::wstring{};

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

        STDMETHODIMP OnStartComposition(ITfCompositionView*, BOOL* ok) noexcept override
        {
            *ok = TRUE;
            if (compositions++ == 0)
            {
                composed = 0;
                ending = false;
                keyboard->CompositionStarted();
            }
            Say("composition started, " + std::to_string(compositions) + " open");
            return S_OK;
        }

        STDMETHODIMP OnUpdateComposition(ITfCompositionView*, ITfRange*) noexcept override
        {
            return S_OK;
        }

        STDMETHODIMP OnEndComposition(ITfCompositionView*) noexcept override
        {
            // The text it ended with is in the document, and reading it
            // wants a cookie there is none of here: the edit it ended
            // in has still to be finished, and that is where it is read.
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
            Take(cookie);
            if (ending)
            {
                ending = false;
                composed = 0;
                keyboard->CompositionCompleted();
            }
            return S_OK;
        }
    };

    TextServices::TextServices() = default;

    TextServices::~TextServices()
    {
        Stop();
    }

    bool TextServices::Start(HWND window, core::input::Keyboard& keyboard, Logger log)
    {
        auto owner = make_self<Owner>();

        owner->window = window;
        owner->keyboard = &keyboard;
        owner->log = std::move(log);
        m_window = window;

        if (FAILED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(m_threads.put()))))
        {
            return false;
        }

        // The input processors are left to be started when the window is
        // given the keys, rather than now, when it has none of them and
        // has not been shown.
        if (FAILED(m_threads->ActivateEx(&m_client, TF_TMAE_NOACTIVATETIP))
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

        // Whenever this window has the keys, the input method is to talk
        // to this document rather than to whatever the window was given
        // by what draws it.
        com_ptr<ITfDocumentMgr> before;
        m_threads->AssociateFocus(window, m_document.get(), before.put());
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
            if (m_window)
            {
                com_ptr<ITfDocumentMgr> before;
                m_threads->AssociateFocus(m_window, nullptr, before.put());
            }
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
        m_window = nullptr;
    }

    void TextServices::NotifyFocusEnter()
    {
        if (m_threads && m_document)
        {
            m_threads->SetFocus(m_document.get());
        }
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
    }

    void TextServices::SetCaret(RECT const& caret)
    {
        if (m_owner)
        {
            m_owner->caret = caret;
        }
    }
}
