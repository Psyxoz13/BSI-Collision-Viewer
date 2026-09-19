#pragma once

#include "model/collision.h"
#include "model/reflection.h"

#include <optional>

constexpr float MetersPerUnit = 0.02f;

#pragma pack(push, 1)
struct CameraPose
{
    Float3 location;
    int32_t pitch;
    int32_t yaw;
    int32_t roll;
    float fieldOfView;

    Matrix viewProjection(float aspect, float fieldOfViewScale, float farPlane) const;
};
#pragma pack(pop)

struct PlayerState
{
    bool valid = false;
    float speed = 0;
    float verticalSpeed = 0;
    float peakSpeed = 0;
    float peakVerticalSpeed = 0;
    uint8_t physics = 0;
    bool collidesWithWorld = false;
    float collisionRadius = 0;
    float collisionHeight = 0;
    Float3 position;
};

class Engine
{
public:
    Engine();

    Reflection reflection;

    std::string describe();
    CameraPose readCamera();
    CharacterSnapshot readCharacters(const std::vector<uint32_t>& cylinders);
    PlayerState readPlayerState();
    Matrix componentMatrix(uint32_t component);
    Matrix rigidBodyMatrix(uint32_t component);
    std::optional<Kind> nonBlockingKind(uint32_t component);
    bool collidesWithoutPhysicsBody(uint32_t component);
    void collect(SceneBuilder& builder);

private:
    const uint32_t engineObject_;
    float peakSpeed_ = 0;
    float peakVerticalSpeed_ = 0;

    uint32_t playerController();
    uint32_t cameraPoseOffset(uint32_t camera);
};
