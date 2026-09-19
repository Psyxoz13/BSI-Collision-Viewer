#include "input/hotkeys.h"

#include "core/common.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <format>

bool Hotkeys::pressed(int key)
{
    bool isDown = (GetAsyncKeyState(key) & 0x8000) != 0;
    bool wasPressed = isDown && !down_[key];
    down_[key] = isDown;
    return wasPressed;
}

void Hotkeys::capture()
{
    for (int key = VK_BACK; key <= 0xFE; key++)
    {
        if (!(GetAsyncKeyState(key) & 0x8000))
        {
            continue;
        }
        if (key != VK_ESCAPE)
        {
            *target_ = key;
            down_[key] = true;
        }
        target_ = nullptr;
        return;
    }
}

std::string Hotkeys::name(int key)
{
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z'))
    {
        return std::string(1, static_cast<char>(key));
    }
    UINT scanCode = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    LONG keyData = static_cast<LONG>(((scanCode & 0xFF) << 16) | ((scanCode & 0xFF00) ? 1 << 24 : 0));
    wchar_t text[64] = {};
    if (scanCode != 0 && GetKeyNameTextW(keyData, text, 64) > 0 &&
        std::all_of(text, text + wcslen(text), [](wchar_t c) { return c < 128; }))
    {
        return utf8(text);
    }
    return std::format("Key {}", key);
}
