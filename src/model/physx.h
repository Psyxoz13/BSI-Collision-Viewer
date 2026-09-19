#pragma once

#include "model/engine.h"

struct ActorList
{
    uint32_t address;
    uint32_t count;
    bool operator==(const ActorList&) const = default;
};

class PhysX
{
public:
    bool connect();
    uint32_t instanceSlot() const { return instanceSlot_; }
    std::vector<ActorList> sceneActorLists();
    Matrix actorMatrix(uint32_t actor);
    void collect(Engine& engine, const std::vector<ActorList>& actorLists, SceneBuilder& builder);

private:
    uint32_t instanceSlot_ = 0;
    uint64_t nextAttempt_ = 0;
    std::string lastFailure_;
};
