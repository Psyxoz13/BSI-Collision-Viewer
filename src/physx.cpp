#include "game.h"

#include <windows.h>

#include <cstring>
#include <format>

using namespace DirectX;

namespace
{
    constexpr uint32_t ScenesOffset = 0x04;
    constexpr uint32_t SceneActorsOffset = 0x113c;
    constexpr uint32_t BodyToActorOffset = 0x24;
    constexpr uint32_t BodyToWorldOffset = 0xf0;
    constexpr uint32_t ActorTypeOffset = 0x04;
    constexpr uint32_t ActorUserDataOffset = 0x0c;
    constexpr uint32_t ActorShapesOffset = 0x1c;
    constexpr uint32_t ShapeCoreOffset = 0x40;
    constexpr uint32_t ConvexHeaderOffset = 0x34;
    constexpr uint32_t TriangleHeaderOffset = 0x10;
    constexpr uint32_t SixteenBitIndicesFlagOffset = 0xbc;
    constexpr uint16_t DynamicActorType = 6;
    constexpr uint16_t StaticActorType = 7;
    constexpr uint8_t SimulationShapeFlag = 1;
    constexpr uint8_t TriggerShapeFlag = 4;
    constexpr uint32_t MaximumScenes = 16;
    constexpr uint32_t MaximumActorsPerScene = 200000;

    enum GeometryType : uint32_t { Sphere, Plane, Capsule, Box, ConvexMesh, TriangleMesh, HeightField };

#pragma pack(push, 1)
    struct RigidPose
    {
        XMFLOAT4 rotation;
        Float3 position;

        Matrix matrix() const
        {
            return store(XMMatrixMultiply(XMMatrixRotationQuaternion(XMLoadFloat4(&rotation)),
                                          XMMatrixTranslation(position.x, position.y, position.z)));
        }
    };

    struct ShapeCore
    {
        RigidPose pose;
        uint32_t geometryType;
        uint8_t geometry[0x2c];
        uint8_t flags;
    };

    struct ConvexMeshHeader
    {
        uint16_t edgeCount;
        uint8_t vertexCount;
        uint8_t polygonCount;
        uint32_t data;
    };

    struct TriangleMeshHeader
    {
        uint32_t vertexCount;
        uint32_t triangleCount;
        uint32_t vertexData;
        uint32_t triangleData;
    };
#pragma pack(pop)

    template <class T>
    T geometryValue(const ShapeCore& core, size_t offset)
    {
        T value;
        memcpy(&value, core.geometry + offset, sizeof value);
        return value;
    }

    Matrix meshScale(const ShapeCore& core)
    {
        XMFLOAT4 rotation = geometryValue<XMFLOAT4>(core, 12);
        Float3 scale = geometryValue<Float3>(core, 0);
        XMMATRIX axes = XMMatrixRotationQuaternion(XMLoadFloat4(&rotation));
        return store(XMMatrixMultiply(XMMatrixMultiply(axes, XMMatrixScaling(scale.x, scale.y, scale.z)), XMMatrixTranspose(axes)));
    }

    Polygons convexMesh(uint32_t mesh)
    {
        constexpr int PolygonRecordSize = 20;
        auto header = read<ConvexMeshHeader>(mesh + ConvexHeaderOffset);
        int edges = header.edgeCount & 0x7fff;
        int polygonBytes = PolygonRecordSize * header.polygonCount;
        int vertexBytes = 12 * header.vertexCount;
        std::vector<uint8_t> buffer = readArray<uint8_t>(header.data, polygonBytes + vertexBytes + 4 * edges);
        const uint8_t* references = buffer.data() + polygonBytes + vertexBytes + 2 * edges;
        Polygons polygons;
        std::vector<Float3> polygon;
        for (int i = 0; i < header.polygonCount; i++)
        {
            uint16_t first = 0;
            memcpy(&first, &buffer[PolygonRecordSize * i + 16], 2);
            int count = buffer[PolygonRecordSize * i + 18];
            if (first + count > 2 * edges)
            {
                continue;
            }
            polygon.assign(count, Float3());
            for (int k = 0; k < count && references[first + k] < header.vertexCount; k++)
            {
                memcpy(&polygon[k], &buffer[polygonBytes + 12 * references[first + k]], 12);
            }
            polygons.add(polygon.data(), count);
        }
        return polygons;
    }

    Polygons triangleMesh(uint32_t mesh)
    {
        auto header = read<TriangleMeshHeader>(mesh + TriangleHeaderOffset);
        bool sixteenBitIndices = read<uint8_t>(mesh + SixteenBitIndicesFlagOffset) != 0;
        std::vector<Float3> vertices = readArray<Float3>(header.vertexData, header.vertexCount);
        std::vector<uint32_t> indices;
        if (sixteenBitIndices)
        {
            std::vector<uint16_t> shortIndices = readArray<uint16_t>(header.triangleData, 3 * int64_t(header.triangleCount));
            indices.assign(shortIndices.begin(), shortIndices.end());
        }
        else
        {
            indices = readArray<uint32_t>(header.triangleData, 3 * int64_t(header.triangleCount));
        }
        Polygons triangles;
        for (uint32_t t = 0; t < header.triangleCount; t++)
        {
            uint32_t a = indices[3 * t], b = indices[3 * t + 1], c = indices[3 * t + 2];
            if (a < header.vertexCount && b < header.vertexCount && c < header.vertexCount)
            {
                triangles.add({vertices[a], vertices[b], vertices[c]});
            }
        }
        return triangles;
    }

    struct ShapeGeometry
    {
        Polygons polygons;
        Kind kind;
        Matrix scale = identity();
    };

    std::optional<ShapeGeometry> readGeometry(const ShapeCore& core)
    {
        switch (core.geometryType)
        {
        case Sphere:
            return ShapeGeometry{shapes::capsule(geometryValue<float>(core, 0), 0), Kind::Primitive};
        case Capsule:
            return ShapeGeometry{shapes::capsule(geometryValue<float>(core, 0), geometryValue<float>(core, 4)), Kind::Primitive};
        case Box:
            return ShapeGeometry{shapes::box(geometryValue<Float3>(core, 0)), Kind::Primitive};
        case ConvexMesh:
            return ShapeGeometry{convexMesh(geometryValue<uint32_t>(core, 0x1c)), Kind::ConvexHull, meshScale(core)};
        case TriangleMesh:
            return ShapeGeometry{triangleMesh(geometryValue<uint32_t>(core, 0x20)), Kind::TriangleMesh, meshScale(core)};
        default:
            return std::nullopt;
        }
    }

    struct ActorOwner
    {
        std::optional<Kind> nonBlockingKind;
        uint32_t rigidBodyComponent = 0;
    };

    ActorOwner readOwner(Engine& engine, uint32_t actor)
    {
        try
        {
            uint32_t component = engine.reflection.field<uint32_t>(read<uint32_t>(actor + ActorUserDataOffset), "OwnerComponent");
            return {engine.nonBlockingKind(component), engine.reflection.isA(component, "SkeletalMeshComponent") ? 0u : component};
        }
        catch (const std::exception& e)
        {
            report("the UE3 side of a PhysX actor", e);
            return {};
        }
    }

    Kind classify(uint8_t flags, bool isDynamic, std::optional<Kind> nonBlockingKind, Kind geometryKind)
    {
        if (flags & TriggerShapeFlag)
        {
            return Kind::Trigger;
        }
        if (nonBlockingKind)
        {
            return *nonBlockingKind;
        }
        if (!(flags & SimulationShapeFlag))
        {
            return Kind::Inactive;
        }
        return isDynamic ? Kind::Dynamic : geometryKind;
    }

    void collectShape(uint32_t shape, uint32_t actor, bool isDynamic, const ActorOwner& owner, SceneBuilder& builder)
    {
        auto core = read<ShapeCore>(shape + ShapeCoreOffset);
        std::optional<ShapeGeometry> geometry = readGeometry(core);
        if (!geometry)
        {
            return;
        }
        Kind kind = classify(core.flags, isDynamic, owner.nonBlockingKind, geometry->kind);
        Source source = !isDynamic ? Source::Static : owner.rigidBodyComponent ? Source::RigidBody : Source::PhysXActor;
        uint32_t target = !isDynamic ? 0 : owner.rigidBodyComponent ? owner.rigidBodyComponent : actor;
        builder.add(source, kind, target, geometry->polygons, mul(geometry->scale, core.pose.matrix()));
    }

    void collectActor(Engine& engine, uint32_t actor, SceneBuilder& builder)
    {
        bool isDynamic = false;
        std::vector<uint32_t> shapes;
        ActorOwner owner;
        try
        {
            uint16_t type = read<uint16_t>(actor + ActorTypeOffset);
            if (type != DynamicActorType && type != StaticActorType)
            {
                return;
            }
            isDynamic = type == DynamicActorType;
            uint32_t shapeList = read<uint32_t>(actor + ActorShapesOffset);
            uint16_t shapeCount = read<uint16_t>(actor + ActorShapesOffset + 4);
            shapes = shapeCount == 1 ? std::vector<uint32_t>{shapeList} : readArray<uint32_t>(shapeList, shapeCount);
            owner = readOwner(engine, actor);
        }
        catch (const std::exception& e)
        {
            report("a PhysX actor", e);
            return;
        }
        for (uint32_t shape : shapes)
        {
            try
            {
                collectShape(shape, actor, isDynamic, owner, builder);
            }
            catch (const std::exception& e)
            {
                report("a PhysX shape", e);
            }
        }
    }

    uint32_t findInstanceSlot(HMODULE module)
    {
        auto code = reinterpret_cast<uint32_t>(GetProcAddress(module, "PxGetPhysics"));
        if (code != 0)
        {
            for (int hop = 0; hop < 4 && read<uint8_t>(code) == 0xE9; hop++)
            {
                code = code + 5 + read<int32_t>(code + 1);
            }
            if (read<uint8_t>(code) == 0xA1 && read<uint8_t>(code + 5) == 0xC3)
            {
                return read<uint32_t>(code + 1);
            }
        }
        throw UnsupportedGame("PxGetPhysics in PhysX3_x86.dll looks different than expected");
    }
}

bool PhysX::connect()
{
    if (instanceSlot_ != 0 || GetTickCount64() < nextAttempt_)
    {
        return instanceSlot_ != 0;
    }
    nextAttempt_ = GetTickCount64() + 5000;
    try
    {
        HMODULE module = GetModuleHandleW(L"PhysX3_x86.dll");
        if (!module)
        {
            throw UnsupportedGame("PhysX3_x86.dll is not loaded yet");
        }
        instanceSlot_ = findInstanceSlot(module);
        if (!lastFailure_.empty())
        {
            logLine("PhysX collision found, drawing it as well");
        }
    }
    catch (const std::exception& e)
    {
        if (e.what() != lastFailure_)
        {
            logLine(std::string("PhysX collision is unavailable, drawing the engine collision only and retrying: ") + e.what());
        }
        lastFailure_ = e.what();
    }
    return instanceSlot_ != 0;
}

std::vector<ActorList> PhysX::sceneActorLists()
{
    uint32_t physics = read<uint32_t>(instanceSlot_);
    uint32_t scenes = read<uint32_t>(physics + ScenesOffset);
    uint32_t count = read<uint32_t>(physics + ScenesOffset + 4);
    if (count > MaximumScenes)
    {
        throw UnsupportedGame("the PhysX scene list looks different than expected");
    }
    std::vector<ActorList> actorLists(count);
    for (uint32_t i = 0; i < count; i++)
    {
        try
        {
            uint32_t scene = read<uint32_t>(scenes + 4 * i);
            actorLists[i] = {read<uint32_t>(scene + SceneActorsOffset), read<uint32_t>(scene + SceneActorsOffset + 4)};
        }
        catch (const MemoryError&)
        {
        }
    }
    return actorLists;
}

Matrix PhysX::actorMatrix(uint32_t actor)
{
    XMMATRIX actorToBody = XMMatrixInverse(nullptr, load(read<RigidPose>(actor + BodyToActorOffset).matrix()));
    return mul(store(actorToBody), read<RigidPose>(actor + BodyToWorldOffset).matrix());
}

void PhysX::collect(Engine& engine, const std::vector<ActorList>& actorLists, SceneBuilder& builder)
{
    for (const ActorList& list : actorLists)
    {
        if (list.count == 0)
        {
            continue;
        }
        std::vector<uint32_t> actors;
        try
        {
            actors = readArray<uint32_t>(list.address, std::min(list.count, MaximumActorsPerScene));
        }
        catch (const std::exception& e)
        {
            report("a PhysX scene", e);
            continue;
        }
        for (uint32_t actor : actors)
        {
            collectActor(engine, actor, builder);
        }
    }
}

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
