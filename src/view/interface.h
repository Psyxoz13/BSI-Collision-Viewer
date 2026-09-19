#pragma once

#include "input/hotkeys.h"
#include "model/engine.h"
#include "model/settings.h"

struct Statistics
{
    int triangles;
    int movingObjects;
    int characters;
};

class Panel
{
public:
    virtual ~Panel() = default;

protected:
    Panel(const char* title, int flags) : title_(title), flags_(flags) {}

    bool render(bool* open = nullptr);

private:
    virtual void place() const = 0;
    virtual void body() = 0;

    const char* title_;
    int flags_;
};

class SettingsPanel : public Panel
{
public:
    SettingsPanel(Settings& settings, Hotkeys& hotkeys);

    bool draw(const Statistics& statistics);

private:
    void place() const override;
    void body() override;

    Settings& settings_;
    Hotkeys& hotkeys_;
    Statistics statistics_ = {};
};

class PlayerStateSettingsPanel : public Panel
{
public:
    PlayerStateSettingsPanel(Settings& settings, Hotkeys& hotkeys);

    void draw();

private:
    void place() const override;
    void body() override;

    Settings& settings_;
    Hotkeys& hotkeys_;
};

class PlayerStatePanel : public Panel
{
public:
    explicit PlayerStatePanel(const Settings& settings);

    void draw(const PlayerState& state);

private:
    void place() const override;
    void body() override;

    const Settings& settings_;
    PlayerState state_;
};
