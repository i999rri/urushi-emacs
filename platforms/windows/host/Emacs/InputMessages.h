#pragma once

#include "Input/KeyNames.h"
#include "Input/PointerMessage.h"

#include <winrt/Windows.Data.Json.h>

#include <string>

namespace urushi::windows::emacs
{
    // The protocol's input messages, for an Emacs that takes the keys,
    // the pointer and the focus as messages (docs/protocol.md).

    winrt::Windows::Data::Json::JsonObject KeyMessage(input::EmacsKey const& key);

    // What the pointer did over the frame FRAME: the panel's, or the
    // empty one for the frame the window shows, which the message then
    // does not name.
    winrt::Windows::Data::Json::JsonObject PointerMessage(std::wstring const& frame,
                                                          input::EmacsPointer const& pointer);

    winrt::Windows::Data::Json::JsonObject FocusMessage(bool focused);
}
