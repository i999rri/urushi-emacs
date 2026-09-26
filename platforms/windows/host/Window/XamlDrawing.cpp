#include "pch.h"

#include "Window/XamlDrawing.h"

#include <Microsoft.UI.Xaml.Media.DxInterop.h>

#include <algorithm>
#include <vector>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using urusi::core::window::DrawCommand;
using urusi::core::window::DrawFrame;
using urusi::core::window::DrawOp;

namespace
{
    D2D1_COLOR_F ColorOf(uint32_t color)
    {
        return D2D1::ColorF(((color >> 16) & 0xff) / 255.0f,
                            ((color >> 8) & 0xff) / 255.0f,
                            (color & 0xff) / 255.0f, 1.0f);
    }

    D2D1_RECT_F BoxOf(int x, int y, int width, int height)
    {
        return D2D1::RectF(static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(x + width),
                           static_cast<float>(y + height));
    }
}

namespace urusi::windows::window
{
    void XamlDrawing::Attach(FrameworkElement const& site)
    {
        auto room = site.try_as<Controls::Panel>();

        if (!room)
        {
            return;
        }

        if (m_panel && m_in == room)
        {
            Follow(site);
            return;
        }

        if (m_panel && m_in)
        {
            uint32_t at{};

            if (m_in.Children().IndexOf(m_panel, at))
            {
                m_in.Children().RemoveAt(at);
            }
        }

        // A panel of its own each time rather than the same one moved:
        // what a swap chain shows is bound to where its panel was, and
        // a panel put somewhere else leaves that behind.  The chain
        // itself goes on, so what was drawn is still there.
        m_panel = Controls::SwapChainPanel();
        m_panel.IsHitTestVisible(false);
        if (m_width > 0 && m_height > 0)
        {
            m_panel.Width(m_width);
            m_panel.Height(m_height);
        }

        // First, so that anything Lisp draws over the frame is drawn
        // over the screen.
        room.Children().InsertAt(0, m_panel);
        m_in = room;
        m_room = winrt::Windows::Foundation::Size{ -1, -1 };
        Follow(site);

        if (m_chain)
        {
            m_panel.as<::ISwapChainPanelNative>()->SetSwapChain(m_chain.get());
            Show();
        }
    }

    void XamlDrawing::Again()
    {
        if (m_chain && m_canvas)
        {
            Show();
        }
    }

    void XamlDrawing::TakeAway()
    {
        uint32_t at{};

        if (m_in && m_panel && m_in.Children().IndexOf(m_panel, at))
        {
            m_in.Children().RemoveAt(at);
        }
        m_in = nullptr;
    }

    void XamlDrawing::Follow(FrameworkElement const& site)
    {
        auto room = winrt::Windows::Foundation::Size{
            static_cast<float>(site.ActualWidth()),
            static_cast<float>(site.ActualHeight())
        };

        if (!m_panel || (room.Width == m_room.Width && room.Height == m_room.Height))
        {
            return;
        }

        m_room = room;

        // Cut to the room the element was given: the frame is taller
        // than its room wherever Lisp draws part of it itself.
        Media::RectangleGeometry clip;
        clip.Rect(winrt::Windows::Foundation::Rect{ 0, 0, room.Width, room.Height });
        m_panel.Clip(clip);
    }

    bool XamlDrawing::Device()
    {
        if (m_context)
        {
            return true;
        }

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        com_ptr<ID3D11Device> d3d;

        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     flags, nullptr, 0, D3D11_SDK_VERSION,
                                     d3d.put(), nullptr, nullptr))
            && FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                        flags, nullptr, 0, D3D11_SDK_VERSION,
                                        d3d.put(), nullptr, nullptr)))
        {
            return false;
        }

        com_ptr<ID2D1Factory7> factory;
        D2D1_FACTORY_OPTIONS options{};

        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                     __uuidof(ID2D1Factory7), &options,
                                     factory.put_void())))
        {
            return false;
        }

        auto dxgi = d3d.as<IDXGIDevice>();
        com_ptr<ID2D1Device6> device;

        if (FAILED(factory->CreateDevice(dxgi.get(), device.put())))
        {
            return false;
        }

        com_ptr<ID2D1DeviceContext5> context;
        if (FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                               reinterpret_cast<ID2D1DeviceContext5**>(
                                                   context.put()))))
        {
            return false;
        }

        m_d3d = d3d;
        m_device = device;
        m_context = context;
        m_context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        m_context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        return true;
    }

    bool XamlDrawing::Ready(int width, int height)
    {
        // Without a panel as well: Emacs draws a frame before the
        // screen saying where it goes has been built, and a child
        // frame is drawn the moment it is made.  What is drawn is kept
        // and shown once there is somewhere to show it, which is what
        // Attach does.
        if (width <= 0 || height <= 0 || !Device())
        {
            return false;
        }

        if (m_chain && m_width == width && m_height == height)
        {
            return true;
        }

        auto properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED));
        auto size = D2D1::SizeU(static_cast<uint32_t>(width),
                                static_cast<uint32_t>(height));

        com_ptr<ID2D1Bitmap1> canvas;
        com_ptr<ID2D1Bitmap1> spare;
        if (FAILED(m_context->CreateBitmap(size, nullptr, 0, properties,
                                           canvas.put()))
            || FAILED(m_context->CreateBitmap(size, nullptr, 0, properties,
                                              spare.put())))
        {
            return false;
        }

        if (m_chain)
        {
            if (FAILED(m_chain->ResizeBuffers(0, static_cast<uint32_t>(width),
                                              static_cast<uint32_t>(height),
                                              DXGI_FORMAT_UNKNOWN, 0)))
            {
                return false;
            }
        }
        else
        {
            DXGI_SWAP_CHAIN_DESC1 desc{};

            desc.Width = static_cast<uint32_t>(width);
            desc.Height = static_cast<uint32_t>(height);
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

            auto dxgi = m_d3d.as<IDXGIDevice>();
            com_ptr<IDXGIAdapter> adapter;
            com_ptr<IDXGIFactory2> factory;

            if (FAILED(dxgi->GetAdapter(adapter.put()))
                || FAILED(adapter->GetParent(__uuidof(IDXGIFactory2),
                                             factory.put_void()))
                || FAILED(factory->CreateSwapChainForComposition(
                    m_d3d.get(), &desc, nullptr, m_chain.put())))
            {
                return false;
            }

            if (m_panel)
            {
                m_panel.as<::ISwapChainPanelNative>()->SetSwapChain(m_chain.get());
            }
        }

        // As many pixels across as Emacs drew for, and as many hundredths
        // of an inch: XAML scales it by the same amount it scales
        // everything else, which is what the picture did before it.
        if (m_panel)
        {
            m_panel.Width(width);
            m_panel.Height(height);
        }

        m_canvas = canvas;
        m_spare = spare;
        m_width = width;
        m_height = height;
        return true;
    }

    com_ptr<ID2D1SolidColorBrush> XamlDrawing::Brush(uint32_t color)
    {
        if (!m_brush)
        {
            m_context->CreateSolidColorBrush(ColorOf(color), m_brush.put());
        }
        else
        {
            m_brush->SetColor(ColorOf(color));
        }
        return m_brush;
    }

    void XamlDrawing::Fill(DrawCommand const& command)
    {
        m_context->FillRectangle(
            BoxOf(command.x, command.y, command.width, command.height),
            Brush(command.color).get());
    }

    void XamlDrawing::Clip(DrawCommand const& command)
    {
        Unclip();
        m_context->PushAxisAlignedClip(
            BoxOf(command.x, command.y, command.width, command.height),
            D2D1_ANTIALIAS_MODE_ALIASED);
        m_clipped = true;
    }

    void XamlDrawing::Unclip()
    {
        if (m_clipped)
        {
            m_context->PopAxisAlignedClip();
            m_clipped = false;
        }
    }

    void XamlDrawing::Glyphs(DrawCommand const& command)
    {
        auto face = m_fonts ? m_fonts->Face(command.font) : nullptr;

        if (!face || command.ids.empty())
        {
            return;
        }

        // Each glyph is put where Emacs put it rather than after the
        // one before it: Emacs laid the text out, and letting the font
        // space it here would space it otherwise.
        std::vector<float> advances(command.ids.size(), 0.0f);
        std::vector<DWRITE_GLYPH_OFFSET> offsets(command.ids.size());

        for (size_t at = 0; at < command.ids.size(); ++at)
        {
            offsets[at].advanceOffset = static_cast<float>(command.xs[at]);
            offsets[at].ascenderOffset = 0.0f;
        }

        DWRITE_GLYPH_RUN run{};
        run.fontFace = face.get();
        run.fontEmSize = static_cast<float>(command.size);
        run.glyphCount = static_cast<uint32_t>(command.ids.size());
        run.glyphIndices = command.ids.data();
        run.glyphAdvances = advances.data();
        run.glyphOffsets = offsets.data();

        m_context->DrawGlyphRun(
            D2D1::Point2F(0.0f, static_cast<float>(command.y)), &run,
            Brush(command.color).get(), DWRITE_MEASURING_MODE_NATURAL);
    }

    void XamlDrawing::Copy(DrawCommand const& command)
    {
        // Direct2D reads one bitmap and writes another, so what moves
        // goes through the spare.
        auto const from = D2D1::RectU(
            static_cast<uint32_t>((std::max)(command.x, 0)),
            static_cast<uint32_t>((std::max)(command.y, 0)),
            static_cast<uint32_t>((std::min)(command.x + command.width, m_width)),
            static_cast<uint32_t>((std::min)(command.y + command.height,
                                             m_height)));
        auto const corner = D2D1::Point2U(from.left, from.top);

        if (from.right <= from.left || from.bottom <= from.top)
        {
            return;
        }

        Unclip();
        m_context->EndDraw();
        m_spare->CopyFromBitmap(&corner, m_canvas.get(), &from);
        m_context->BeginDraw();

        auto const box = BoxOf(command.x, command.toY, command.width,
                               command.height);
        m_context->DrawBitmap(
            m_spare.get(), box, 1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
            BoxOf(command.x, command.y, command.width, command.height));
    }

    void XamlDrawing::Show()
    {
        com_ptr<IDXGISurface> back;
        com_ptr<ID2D1Bitmap1> target;

        // Nowhere to show it yet: it stays in the canvas until there
        // is, and Attach shows it then.
        if (!m_chain || !m_panel)
        {
            return;
        }

        if (FAILED(m_chain->GetBuffer(0, __uuidof(IDXGISurface), back.put_void())))
        {
            return;
        }

        auto properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_IGNORE));

        if (FAILED(m_context->CreateBitmapFromDxgiSurface(back.get(), properties,
                                                          target.put())))
        {
            return;
        }

        m_context->SetTarget(target.get());
        m_context->BeginDraw();
        m_context->DrawImage(m_canvas.get());
        m_context->EndDraw();

        // Nothing may still be holding the buffer about to be shown.
        m_context->SetTarget(nullptr);
        target = nullptr;
        back = nullptr;

        // Without waiting for the screen to turn over: this is the
        // thread that lays the window out, and waiting here would be
        // the window waiting.
        m_chain->Present(0, 0);
    }

    std::string XamlDrawing::Draw(DrawFrame const& said)
    {
        if (!Ready(said.width, said.height))
        {
            return "no drawing for a screen of " + std::to_string(said.width)
                + "x" + std::to_string(said.height);
        }

        m_context->SetTarget(m_canvas.get());
        m_context->BeginDraw();
        m_drawing = true;
        m_clipped = false;

        for (auto const& command : said.commands)
        {
            switch (command.op)
            {
            case DrawOp::Fill:
                Fill(command);
                break;

            case DrawOp::Rectangle:
                m_context->DrawRectangle(
                    D2D1::RectF(command.x + 0.5f, command.y + 0.5f,
                                command.x + command.width + 0.5f,
                                command.y + command.height + 0.5f),
                    Brush(command.color).get(), 1.0f);
                break;

            case DrawOp::Line:
                m_context->DrawLine(
                    D2D1::Point2F(command.x + 0.5f, command.y + 0.5f),
                    D2D1::Point2F(command.width + 0.5f, command.height + 0.5f),
                    Brush(command.color).get(), 1.0f);
                break;

            case DrawOp::Copy:
                Copy(command);
                break;

            case DrawOp::Clip:
                Clip(command);
                break;

            case DrawOp::Unclip:
                Unclip();
                break;

            case DrawOp::Glyphs:
                Glyphs(command);
                break;
            }
        }

        Unclip();
        auto const why = m_context->EndDraw();
        m_drawing = false;
        m_context->SetTarget(nullptr);

        if (FAILED(why))
        {
            return "drawing gave up";
        }

        Show();
        return {};
    }
}
