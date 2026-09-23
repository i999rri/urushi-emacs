#include "pch.h"
#include "Window/XamlScreen.h"

#include "Window/Rows.h"

#include <limits>
#include <optional>
#include <string>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Windows::Data::Json;

namespace
{
    // What a stretch of a row turns into under the pointer, as Lisp put
    // it on the element that draws it: "hover:FOREGROUND:BACKGROUND",
    // either of which may be empty where Emacs said nothing about it.
    struct Hover
    {
        std::wstring foreground;
        std::wstring background;
    };

    std::optional<Hover> HoverOf(FrameworkElement const& element)
    {
        auto tag = element.Tag().try_as<hstring>();
        if (!tag)
        {
            return std::nullopt;
        }

        std::wstring text{ tag->c_str() };
        if (text.rfind(L"hover:", 0) != 0)
        {
            return std::nullopt;
        }

        std::wstring rest = text.substr(6);
        auto between = rest.find(L':');
        if (between == std::wstring::npos)
        {
            return std::nullopt;
        }
        return Hover{ rest.substr(0, between), rest.substr(between + 1) };
    }

    Media::Brush BrushOf(std::wstring const& colour)
    {
        if (colour.size() != 7 || colour[0] != L'#')
        {
            return nullptr;
        }

        unsigned long value = std::wcstoul(colour.c_str() + 1, nullptr, 16);
        Windows::UI::Color rgb{};

        rgb.A = 255;
        rgb.R = static_cast<uint8_t>((value >> 16) & 0xFF);
        rgb.G = static_cast<uint8_t>((value >> 8) & 0xFF);
        rgb.B = static_cast<uint8_t>(value & 0xFF);
        return Media::SolidColorBrush{ rgb };
    }

    // Show what Emacs said a stretch of a row turns into while the
    // pointer is over it.
    //
    // Where the pointer is is the window's to know, so it shows this
    // itself rather than telling Emacs and waiting to be told what to
    // draw. Emacs is told all the same, for the help it shows there and
    // for what a click comes to; only the colour is answered here.
    void AttachHover(UIElement const& element)
    {
        auto border = element.try_as<Controls::Border>();

        if (auto framework = element.try_as<FrameworkElement>(); framework && border)
        {
            if (auto hover = HoverOf(framework))
            {
                auto text = border.Child().try_as<Controls::TextBlock>();
                auto background = BrushOf(hover->background);
                auto foreground = BrushOf(hover->foreground);
                auto wasBackground = border.Background();
                auto wasForeground = text ? text.Foreground() : nullptr;
                // Weakly, or the handler and the element would hold each
                // other and a row would never go.
                auto weakBorder = make_weak(border);

                border.PointerEntered([weakBorder, text, background, foreground](
                                          Windows::Foundation::IInspectable const&, Input::PointerRoutedEventArgs const&) {
                    if (auto it = weakBorder.get(); it && background)
                    {
                        it.Background(background);
                    }
                    if (text && foreground)
                    {
                        text.Foreground(foreground);
                    }
                });
                border.PointerExited([weakBorder, text, wasBackground, wasForeground](
                                         Windows::Foundation::IInspectable const&, Input::PointerRoutedEventArgs const&) {
                    if (auto it = weakBorder.get())
                    {
                        it.Background(wasBackground);
                    }
                    if (text)
                    {
                        text.Foreground(wasForeground);
                    }
                });
            }
        }

        if (auto panel = element.try_as<Controls::Panel>())
        {
            for (auto const& child : panel.Children())
            {
                AttachHover(child);
            }
        }
        else if (border && border.Child())
        {
            AttachHover(border.Child());
        }
    }
}

namespace urusi::windows::window
{
    XamlScreen::XamlScreen(Controls::Panel const& surface, std::shared_ptr<emacs::Emacs> emacs)
        : m_surface(surface), m_emacs(std::move(emacs))
    {
    }

    bool XamlScreen::ShowChrome(JsonObject const& message)
    {
        // The XAML around the rows comes only when it has changed, and
        // everything in it goes with it.
        if (!message.HasKey(L"xaml"))
        {
            return false;
        }

        UIElement root{ nullptr };
        try
        {
            root = Markup::XamlReader::Load(message.GetNamedString(L"xaml", L"")).as<UIElement>();
        }
        catch (hresult_error const& e)
        {
            m_emacs->SendError(L"XAML: " + e.message());
            return false;
        }

        m_surface.Children().Clear();
        m_surface.Children().Append(root);

        if (auto element = root.try_as<FrameworkElement>())
        {
            AttachEvents(element, message.GetNamedArray(L"events", JsonArray{}));
        }
        return true;
    }

    void XamlScreen::ShowRows(JsonObject const& message)
    {
        auto root = Root();

        for (auto const& value : message.GetNamedArray(L"rows", JsonArray{}))
        {
            auto group = value.GetObject();
            auto name = group.GetNamedString(L"panel", L"");

            if (auto panel = root ? FindPanel(root, name) : nullptr)
            {
                ReconcileRows(panel, group.GetNamedArray(L"items", JsonArray{}));
            }
            else
            {
                m_emacs->SendError(L"no panel named " + name);
            }
        }
    }

    FrameworkElement XamlScreen::Named(hstring const& name) const
    {
        auto root = Root();
        auto found = root ? root.FindName(name) : nullptr;
        return found ? found.try_as<FrameworkElement>() : nullptr;
    }

    void XamlScreen::Measure(JsonObject const& message) const
    {
        constexpr int kSample = 100;

        auto family = message.GetNamedString(L"family", L"Consolas");
        double size = message.GetNamedNumber(L"size", 14);

        // One of each kind that Emacs counts differently: a character
        // of one column and one of two. Emacs says which font a run is
        // drawn in, so both are measured in the font asked about.
        auto advance = [&](wchar_t sample) {
            Controls::TextBlock block;
            block.FontFamily(Media::FontFamily{ family });
            block.FontSize(size);
            block.Text(hstring{ std::wstring(kSample, sample) });
            block.Measure({ std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::infinity() });
            return block.DesiredSize().Width / kSample;
        };

        JsonObject reply;
        reply.SetNamedValue(L"type", JsonValue::CreateStringValue(L"measured"));
        reply.SetNamedValue(L"family", JsonValue::CreateStringValue(family));
        reply.SetNamedValue(L"size", JsonValue::CreateNumberValue(size));
        reply.SetNamedValue(L"narrow", JsonValue::CreateNumberValue(advance(L'0')));
        // HIRAGANA LETTER A, spelled out: this file is read as bytes.
        reply.SetNamedValue(L"wide", JsonValue::CreateNumberValue(advance(L'\x3042')));
        m_emacs->Send(reply);
    }

    FrameworkElement XamlScreen::Root() const
    {
        return m_surface.Children().Size() ? m_surface.Children().GetAt(0).try_as<FrameworkElement>()
                                           : nullptr;
    }

    // The panel named NAME under ROOT. The XAML around the rows knows the
    // names in it, but a row is read on its own and keeps its names to
    // itself, so a panel inside a row is looked for among the rows.
    // Rows come before the rows inside them, so it is there by now.
    Controls::Panel XamlScreen::FindPanel(FrameworkElement const& root, hstring const& name) const
    {
        if (auto found = root.FindName(name))
        {
            return found.try_as<Controls::Panel>();
        }

        std::vector<UIElement> pending{ root };
        while (!pending.empty())
        {
            auto element = pending.back();
            pending.pop_back();

            auto panel = element.try_as<Controls::Panel>();
            if (panel && panel.Name() == name)
            {
                return panel;
            }
            if (panel)
            {
                for (auto const& child : panel.Children())
                {
                    pending.push_back(child);
                }
            }
            else if (auto border = element.try_as<Controls::Border>(); border && border.Child())
            {
                pending.push_back(border.Child());
            }
        }
        return nullptr;
    }

    // Put the rows of PANEL in the order ITEMS gives, building the ones
    // that came with XAML of their own and keeping the ones that did
    // not. A row is known by its key, which it carries in its Tag.
    void XamlScreen::ReconcileRows(Controls::Panel const& panel, JsonArray const& items)
    {
        auto children = panel.Children();

        std::vector<std::wstring> existing;
        existing.reserve(children.Size());
        for (auto const& child : children)
        {
            auto element = child.try_as<FrameworkElement>();
            existing.emplace_back(element ? unbox_value_or<hstring>(element.Tag(), L"") : L"");
        }

        std::vector<core::window::RowItem> rows;
        rows.reserve(items.Size());
        for (auto const& value : items)
        {
            auto item = value.GetObject();
            rows.push_back({ std::wstring{ item.GetNamedString(L"key", L"") },
                             item.HasKey(L"xaml") });
        }

        auto plan = core::window::PlanRows(existing, rows);
        if (plan.stale)
        {
            // Emacs thinks this window shows something it does not. Ask
            // for the whole screen again.
            JsonObject stale;
            stale.SetNamedValue(L"type", JsonValue::CreateStringValue(L"stale"));
            m_emacs->Send(stale);
            return;
        }

        std::vector<UIElement> wanted;
        wanted.reserve(items.Size());
        for (uint32_t i = 0; i < items.Size(); i++)
        {
            if (auto kept = plan.sources[i].kept)
            {
                wanted.push_back(children.GetAt(*kept));
                continue;
            }

            auto item = items.GetObjectAt(i);
            auto key = item.GetNamedString(L"key", L"");
            UIElement row{ nullptr };
            try
            {
                row = Markup::XamlReader::Load(item.GetNamedString(L"xaml", L"")).as<UIElement>();
            }
            catch (hresult_error const& e)
            {
                // One row that will not parse is one row missing, not a
                // screen lost.
                m_emacs->SendError(L"row " + key + L": " + e.message());
                continue;
            }
            auto element = row.as<FrameworkElement>();
            element.Tag(box_value(key));
            // The row is read on its own, and its names are its own: only
            // the row can find what its events are on.
            AttachEvents(element, item.GetNamedArray(L"events", JsonArray{}));
            AttachHover(row);
            wanted.push_back(row);
        }

        // The panel's children, in the few calls core::window::Arrange makes.
        struct Children
        {
            Controls::UIElementCollection list;

            uint32_t Size() const { return list.Size(); }
            UIElement At(uint32_t i) const { return list.GetAt(i); }
            bool IndexOf(UIElement const& x, uint32_t& at) const { return list.IndexOf(x, at); }
            void RemoveAt(uint32_t i) { list.RemoveAt(i); }
            void InsertAt(uint32_t i, UIElement const& x) { list.InsertAt(i, x); }
            void RemoveAtEnd() { list.RemoveAtEnd(); }
        } list{ children };
        core::window::Arrange(list, wanted);
    }

    void XamlScreen::AttachEvents(FrameworkElement const& root, JsonArray const& events) const
    {
        using namespace Microsoft::UI::Xaml::Controls;

        for (auto const& value : events)
        {
            auto entry = value.GetObject();
            auto name = entry.GetNamedString(L"name", L"");
            auto event = entry.GetNamedString(L"event", L"");
            auto id = entry.GetNamedString(L"id", L"");

            // An element may be the row itself, which FindName does not
            // look at: it looks among what is inside.
            winrt::Windows::Foundation::IInspectable target =
                root.Name() == name ? winrt::Windows::Foundation::IInspectable{ root } : root.FindName(name);
            if (!target)
            {
                m_emacs->SendError(L"no element named " + name);
                continue;
            }

            // Emacs is held weakly: an element Lisp built can outlive
            // the window that holds it.
            std::weak_ptr<emacs::Emacs> weak = m_emacs;
            if (event == L"Click")
            {
                if (auto button = target.try_as<Primitives::ButtonBase>())
                {
                    button.Click([weak, id](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                        if (auto emacs = weak.lock())
                        {
                            emacs->SendEvent(id, JsonObject{});
                        }
                    });
                    continue;
                }
            }
            else if (event == L"TextChanged")
            {
                if (auto box = target.try_as<TextBox>())
                {
                    box.TextChanged([weak, id](winrt::Windows::Foundation::IInspectable const& sender, TextChangedEventArgs const&) {
                        if (auto emacs = weak.lock())
                        {
                            JsonObject args;
                            args.SetNamedValue(L"text", JsonValue::CreateStringValue(
                                sender.as<TextBox>().Text()));
                            emacs->SendEvent(id, args);
                        }
                    });
                    continue;
                }
            }
            else if (event == L"SelectionChanged")
            {
                if (auto selector = target.try_as<Primitives::Selector>())
                {
                    selector.SelectionChanged([weak, id](winrt::Windows::Foundation::IInspectable const& sender, SelectionChangedEventArgs const&) {
                        if (auto emacs = weak.lock())
                        {
                            JsonObject args;
                            args.SetNamedValue(L"index", JsonValue::CreateNumberValue(
                                sender.as<Primitives::Selector>().SelectedIndex()));
                            emacs->SendEvent(id, args);
                        }
                    });
                    continue;
                }
            }
            else if (event == L"Toggled")
            {
                if (auto toggle = target.try_as<ToggleSwitch>())
                {
                    toggle.Toggled([weak, id](winrt::Windows::Foundation::IInspectable const& sender, RoutedEventArgs const&) {
                        if (auto emacs = weak.lock())
                        {
                            JsonObject args;
                            args.SetNamedValue(L"on", JsonValue::CreateBooleanValue(
                                sender.as<ToggleSwitch>().IsOn()));
                            emacs->SendEvent(id, args);
                        }
                    });
                    continue;
                }
            }

            m_emacs->SendError(L"cannot attach " + event + L" to " + name);
        }
    }
}
