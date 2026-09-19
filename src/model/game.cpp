#include "model/game.h"

#include <format>

Game::Game()
{
    physX.connect();
}

std::string Game::describe()
{
    return std::format("PhysX instance slot 0x{:x}, {}", physX.instanceSlot(), engine.describe());
}

std::vector<ActorList> Game::sceneSignature()
{
    try
    {
        return physX.connect() ? physX.sceneActorLists() : std::vector<ActorList>();
    }
    catch (const std::exception& e)
    {
        report("the PhysX scene list", e);
        return {};
    }
}

Scene Game::buildCollision(const std::vector<ActorList>& signature)
{
    SceneBuilder builder;
    auto collect = [](const char* description, auto source)
    {
        try
        {
            source();
        }
        catch (const std::exception& e)
        {
            report(description, e);
        }
    };
    if (physX.instanceSlot() != 0)
    {
        collect("the PhysX collision", [&] { physX.collect(engine, signature, builder); });
    }
    collect("the engine collision", [&] { engine.collect(builder); });
    collect("the character shapes", [&]
    {
        for (Kind kind : {Kind::Character, Kind::Inactive, Kind::Trigger, Kind::BulletsOnly})
        {
            builder.add(Source::Character, kind, 0, shapes::unitCylinder(), identity());
        }
    });
    return builder.build();
}

void Game::track(Scene& scene)
{
    scene.track([&](const Group& group)
    {
        switch (group.source)
        {
        case Source::PhysXActor:
            return physX.actorMatrix(group.target);
        case Source::RigidBody:
            return engine.rigidBodyMatrix(group.target);
        default:
            return engine.componentMatrix(group.target);
        }
    });
}
