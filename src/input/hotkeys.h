#pragma once

#include <string>

class Hotkeys
{
public:
    bool pressed(int binding);
    bool isBinding() const { return target_ != nullptr; }
    bool isBinding(const int& key) const { return target_ == &key; }
    void toggleBinding(int& key) { target_ = target_ == &key ? nullptr : &key; }
    void stopBinding() { target_ = nullptr; }
    void capture();

    static std::string name(int binding);

private:
    bool down_[256] = {};
    int* target_ = nullptr;
};
