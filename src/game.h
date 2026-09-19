#pragma once

#include "collision.h"

#include <mutex>
#include <optional>
#include <unordered_map>

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

struct Property
{
    uint32_t offset;
    uint32_t typeData;
};

class Reflection
{
public:
    struct Element
    {
        uint32_t address;
        uint32_t structure;
    };

    Reflection(std::pair<uint32_t, uint32_t> namesAndObjects);

    const uint32_t namesAddress;
    const uint32_t objectsAddress;

    const std::string& nameOf(uint32_t object);
    bool isDefaultObject(uint32_t object);
    uint32_t classOf(uint32_t object);
    bool isA(uint32_t object, const char* className);
    bool derives(uint32_t structure, const char* baseName);
    Property property(uint32_t structure, const char* name);
    uint32_t offsetOf(uint32_t object, const char* name) { return property(classOf(object), name).offset; }
    bool flag(uint32_t object, const char* name);
    std::vector<Element> elements(uint32_t owner, uint32_t ownerStructure, const char* arrayName);
    std::vector<uint32_t> allObjects();

    template <class T>
    T field(uint32_t object, const char* name) { return read<T>(object + offsetOf(object, name)); }

    template <class T>
    T elementField(Element element, const char* name) { return read<T>(element.address + property(element.structure, name).offset); }

    template <class T>
    std::vector<T> elementArray(Element element, const char* name)
    {
        return readCountedArray<T>(element.address + property(element.structure, name).offset);
    }

private:
    const std::string& name(int index);

    std::mutex mutex_;
    std::unordered_map<int, std::string> names_;
    std::map<std::pair<uint32_t, std::string>, Property> properties_;
};

class Engine
{
public:
    Engine();

    Reflection reflection;

    std::string describe();
    CameraPose readCamera();
    CharacterSnapshot readCharacters(const std::vector<uint32_t>& cylinders);
    Matrix componentMatrix(uint32_t component);
    Matrix rigidBodyMatrix(uint32_t component);
    std::optional<Kind> nonBlockingKind(uint32_t component);
    bool collidesWithoutPhysicsBody(uint32_t component);
    void collect(SceneBuilder& builder);

private:
    const uint32_t engineObject_;

    uint32_t playerController();
    uint32_t cameraPoseOffset(uint32_t camera);
};

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
