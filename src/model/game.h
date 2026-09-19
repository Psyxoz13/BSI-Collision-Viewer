#pragma once

#include "model/engine.h"
#include "model/physx.h"

class Game
{
public:
    Game();

    Engine engine;
    PhysX physX;

    std::string describe();
    std::vector<ActorList> sceneSignature();
    Scene buildCollision(const std::vector<ActorList>& signature);
    void track(Scene& scene);
};
