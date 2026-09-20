#include "input/hotkeys.h"

#include "core/common.h"
#include "model/settings.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <format>

namespace
{
    int modifiersDown()
    {
        return ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? Hotkey::Ctrl : 0) |
               ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? Hotkey::Shift : 0) |
               ((GetAsyncKeyState(VK_MENU) & 0x8000) ? Hotkey::Alt : 0);
    }

    bool isModifierKey(int key)
    {
        return key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU || (key >= VK_LSHIFT && key <= VK_RMENU);
    }
}

bool Hotkeys::pressed(int binding)
{
    int key = binding & Hotkey::KeyMask;
    bool isDown = (GetAsyncKeyState(key) & 0x8000) != 0;
    bool wasPressed = isDown && !down_[key] && modifiersDown() == (binding & Hotkey::ModifierMask);
    down_[key] = isDown;
    return wasPressed;
}

void Hotkeys::capture()
{
    for (int key = VK_BACK; key <= 0xFE; key++)
    {
        if (!(GetAsyncKeyState(key) & 0x8000) || isModifierKey(key))
        {
            continue;
        }
        if (key != VK_ESCAPE)
        {
            *target_ = key | modifiersDown();
            down_[key] = true;
        }
        target_ = nullptr;
        return;
    }
}

std::string Hotkeys::name(int binding)
{
    std::string prefix;
    if (binding & Hotkey::Ctrl)
    {
        prefix += "Ctrl+";
    }
    if (binding & Hotkey::Shift)
    {
        prefix += "Shift+";
    }
    if (binding & Hotkey::Alt)
    {
        prefix += "Alt+";
    }
    int key = binding & Hotkey::KeyMask;
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z'))
    {
        return prefix + static_cast<char>(key);
    }
    UINT scanCode = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    LONG keyData = static_cast<LONG>(((scanCode & 0xFF) << 16) | ((scanCode & 0xFF00) ? 1 << 24 : 0));
    wchar_t text[64] = {};
    if (scanCode != 0 && GetKeyNameTextW(keyData, text, 64) > 0 &&
        std::all_of(text, text + wcslen(text), [](wchar_t c) { return c < 128; }))
    {
        return prefix + utf8(text);
    }
    return prefix + std::format("Key {}", key);
}
