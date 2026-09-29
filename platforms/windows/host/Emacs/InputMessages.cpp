#include "pch.h"
#include "Emacs/InputMessages.h"

using namespace winrt;
using namespace Windows::Data::Json;

namespace
{
    JsonValue String(std::string const& text)
    {
        return JsonValue::CreateStringValue(to_hstring(text));
    }

    JsonArray Modifiers(std::vector<std::string> const& names)
    {
        JsonArray array;
        for (auto const& name : names)
        {
            array.Append(String(name));
        }
        return array;
    }

    // CHARACTER as the UTF-16 JSON strings are made of.
    std::wstring Utf16(char32_t character)
    {
        if (character < 0x10000)
        {
            return std::wstring(1, static_cast<wchar_t>(character));
        }
        character -= 0x10000;
        return { static_cast<wchar_t>(0xD800 + (character >> 10)),
                 static_cast<wchar_t>(0xDC00 + (character & 0x3FF)) };
    }
}

namespace urushi::windows::emacs
{
    JsonObject KeyMessage(input::EmacsKey const& key)
    {
        JsonObject message;
        message.SetNamedValue(L"type", String("key"));
        message.SetNamedValue(L"down", JsonValue::CreateBooleanValue(key.down));
        if (key.name.empty())
        {
            message.SetNamedValue(L"char",
                                  JsonValue::CreateStringValue(hstring{ Utf16(key.character) }));
        }
        else
        {
            message.SetNamedValue(L"name", String(key.name));
        }
        message.SetNamedValue(L"modifiers", Modifiers(key.modifiers));
        message.SetNamedValue(L"repeat", JsonValue::CreateBooleanValue(key.repeat));
        return message;
    }

    JsonObject PointerMessage(std::wstring const& frame, input::EmacsPointer const& pointer)
    {
        JsonObject message;
        message.SetNamedValue(L"type", String("pointer"));
        if (!frame.empty())
        {
            message.SetNamedValue(L"frame", JsonValue::CreateStringValue(hstring{ frame }));
        }
        message.SetNamedValue(L"kind", String(pointer.kind));
        if (pointer.button)
        {
            message.SetNamedValue(L"button", JsonValue::CreateNumberValue(pointer.button));
        }
        message.SetNamedValue(L"x", JsonValue::CreateNumberValue(pointer.x));
        message.SetNamedValue(L"y", JsonValue::CreateNumberValue(pointer.y));
        if (pointer.kind == "wheel")
        {
            message.SetNamedValue(L"dx", JsonValue::CreateNumberValue(pointer.dx));
            message.SetNamedValue(L"dy", JsonValue::CreateNumberValue(pointer.dy));
        }
        message.SetNamedValue(L"modifiers", Modifiers(pointer.modifiers));
        return message;
    }

    JsonObject FocusMessage(bool focused)
    {
        JsonObject message;
        message.SetNamedValue(L"type", String("focus"));
        message.SetNamedValue(L"focused", JsonValue::CreateBooleanValue(focused));
        return message;
    }
}
