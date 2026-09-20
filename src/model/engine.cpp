#include "model/engine.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <format>

using namespace DirectX;

namespace
{
    constexpr uint32_t CameraRotationOffset = 12;
    constexpr uint32_t CameraFieldOfViewOffset = 24;

    uint32_t findEngineObject(Reflection& reflection)
    {
        std::unordered_map<uint32_t, bool> engineClasses;
        for (uint32_t object : reflection.allObjects())
        {
            if (object == 0)
            {
                continue;
            }
            try
            {
                uint32_t objectClass = reflection.classOf(object);
                auto known = engineClasses.find(objectClass);
                if (known == engineClasses.end())
                {
                    known = engineClasses.emplace(objectClass, reflection.derives(objectClass, "GameEngine")).first;
                }
                if (known->second && !reflection.isDefaultObject(object))
                {
                    return object;
                }
            }
            catch (const MemoryError&)
            {
            }
        }
        throw UnsupportedGame("the engine object was not found");
    }
}

Matrix CameraPose::viewProjection(float aspect, float fieldOfViewScale, float farPlane) const
{
    constexpr float RotationUnitsToRadians = XM_PI / 32768;
    constexpr float NearPlane = 0.1f;
    float sinPitch = sinf(pitch * RotationUnitsToRadians), cosPitch = cosf(pitch * RotationUnitsToRadians);
    float sinYaw = sinf(yaw * RotationUnitsToRadians), cosYaw = cosf(yaw * RotationUnitsToRadians);
    float sinRoll = sinf(roll * RotationUnitsToRadians), cosRoll = cosf(roll * RotationUnitsToRadians);
    XMVECTOR right = XMVectorSet(sinRoll * sinPitch * cosYaw - cosRoll * sinYaw, sinRoll * sinPitch * sinYaw + cosRoll * cosYaw, -sinRoll * cosPitch, 0);
    XMVECTOR up = XMVectorSet(-(cosRoll * sinPitch * cosYaw + sinRoll * sinYaw), cosYaw * sinRoll - cosRoll * sinPitch * sinYaw, cosRoll * cosPitch, 0);
    XMVECTOR forward = XMVectorSet(cosPitch * cosYaw, cosPitch * sinYaw, sinPitch, 0);
    XMVECTOR eye = XMVectorScale(XMLoadFloat3(&location), MetersPerUnit);
    XMMATRIX view(XMVectorGetX(right), XMVectorGetY(right), XMVectorGetZ(right), -XMVectorGetX(XMVector3Dot(right, eye)),
                  XMVectorGetX(up), XMVectorGetY(up), XMVectorGetZ(up), -XMVectorGetX(XMVector3Dot(up, eye)),
                  XMVectorGetX(forward), XMVectorGetY(forward), XMVectorGetZ(forward), -XMVectorGetX(XMVector3Dot(forward, eye)),
                  0, 0, 0, 1);
    float tangent = tanf(fieldOfView * fieldOfViewScale * XM_PI / 360) * 9 / 16;
    XMMATRIX projection(1 / (tangent * aspect), 0, 0, 0,
                        0, 1 / tangent, 0, 0,
                        0, 0, farPlane / (farPlane - NearPlane), -farPlane * NearPlane / (farPlane - NearPlane),
                        0, 0, 1, 0);
    return store(XMMatrixTranspose(XMMatrixMultiply(projection, view)));
}

Engine::Engine() : reflection(locateTables()), engineObject_(findEngineObject(reflection))
{
}

std::string Engine::describe()
{
    auto hex = [](auto readValue)
    {
        try
        {
            return std::format("0x{:x}", readValue());
        }
        catch (const std::exception& e)
        {
            return std::format("unavailable ({})", e.what());
        }
    };
    return std::format("name table 0x{:x}, engine object 0x{:x}, camera offsets GamePlayers {} POV {}",
                       reflection.namesAddress, engineObject_,
                       hex([&] { return reflection.offsetOf(engineObject_, "GamePlayers"); }),
                       hex([&] { return cameraPoseOffset(reflection.field<uint32_t>(playerController(), "PlayerCamera")); }));
}

CameraPose Engine::readCamera()
{
    uint32_t camera = reflection.field<uint32_t>(playerController(), "PlayerCamera");
    return read<CameraPose>(camera + cameraPoseOffset(camera));
}

CharacterSnapshot Engine::readCharacters(const std::vector<uint32_t>& cylinders)
{
    CharacterSnapshot snapshot;
    uint32_t playerPawn = 0;
    try
    {
        playerPawn = reflection.field<uint32_t>(playerController(), "Pawn");
    }
    catch (const std::exception& e)
    {
        report("the player", e);
        return snapshot;
    }
    for (uint32_t cylinder : cylinders)
    {
        try
        {
            if (!reflection.flag(cylinder, "bAttached") || reflection.field<uint32_t>(cylinder, "Owner") == playerPawn)
            {
                continue;
            }
            float radius = reflection.field<float>(cylinder, "CollisionRadius") * MetersPerUnit;
            float height = reflection.field<float>(cylinder, "CollisionHeight") * MetersPerUnit;
            Matrix toWorld = reflection.field<Matrix>(cylinder, "LocalToWorld");
            Float3 position(toWorld._41 * MetersPerUnit, toWorld._42 * MetersPerUnit, toWorld._43 * MetersPerUnit);
            Kind kind = nonBlockingKind(cylinder).value_or(Kind::Character);
            snapshot.transforms[static_cast<int>(kind)].push_back(mul(scaling(radius, radius, height), translation(position)));
            snapshot.count++;
        }
        catch (const std::exception& e)
        {
            report("a character", e);
        }
    }
    return snapshot;
}

PlayerState Engine::readPlayerState()
{
    constexpr float StandingStill = 1;
    PlayerState state;
    uint32_t pawn = reflection.field<uint32_t>(playerController(), "Pawn");
    if (pawn == 0)
    {
        peakSpeed_ = peakVerticalSpeed_ = 0;
        return state;
    }
    Float3 velocity = reflection.field<Float3>(pawn, "Velocity");
    state.speed = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
    state.verticalSpeed = velocity.z;
    if (state.speed < StandingStill && fabsf(state.verticalSpeed) < StandingStill)
    {
        peakSpeed_ = peakVerticalSpeed_ = 0;
    }
    peakSpeed_ = std::max(peakSpeed_, state.speed);
    peakVerticalSpeed_ = std::max(peakVerticalSpeed_, state.verticalSpeed);
    state.peakSpeed = peakSpeed_;
    state.peakVerticalSpeed = peakVerticalSpeed_;
    state.valid = true;

    auto tryRead = [](const char* what, auto&& read)
    {
        try
        {
            read();
        }
        catch (const std::exception& e)
        {
            report(what, e);
        }
    };
    tryRead("the player's physics state", [&] { state.physics = reflection.field<uint8_t>(pawn, "Physics"); });
    tryRead("the player's world collision", [&] { state.collidesWithWorld = reflection.flag(pawn, "bCollideWorld"); });
    tryRead("the player's cylinder", [&]
    {
        uint32_t cylinder = reflection.field<uint32_t>(pawn, "CylinderComponent");
        if (cylinder == 0)
        {
            return;
        }
        state.collisionRadius = reflection.field<float>(cylinder, "CollisionRadius");
        state.collisionHeight = reflection.field<float>(cylinder, "CollisionHeight");
        Matrix toWorld = reflection.field<Matrix>(cylinder, "LocalToWorld");
        state.position = Float3(toWorld._41, toWorld._42, toWorld._43);
    });
    return state;
}

Matrix Engine::componentMatrix(uint32_t component)
{
    return mul(reflection.field<Matrix>(component, "LocalToWorld"), scaling(MetersPerUnit, MetersPerUnit, MetersPerUnit));
}

Matrix Engine::rigidBodyMatrix(uint32_t component)
{
    XMVECTOR scale, rotation, position;
    XMMatrixDecompose(&scale, &rotation, &position, load(reflection.field<Matrix>(component, "LocalToWorld")));
    return store(XMMatrixMultiply(XMMatrixRotationQuaternion(rotation), XMMatrixTranslationFromVector(XMVectorScale(position, MetersPerUnit))));
}

std::optional<Kind> Engine::nonBlockingKind(uint32_t component)
{
    uint32_t owner = reflection.field<uint32_t>(component, "Owner");
    if (!reflection.flag(owner, "bCollideActors") || !reflection.flag(component, "CollideActors"))
    {
        return Kind::Inactive;
    }
    if (!reflection.flag(owner, "bBlockActors") || !reflection.flag(component, "BlockActors"))
    {
        return Kind::Trigger;
    }
    return reflection.flag(component, "BlockNonZeroExtent") ? std::nullopt : std::optional(Kind::BulletsOnly);
}

bool Engine::collidesWithoutPhysicsBody(uint32_t component)
{
    uint32_t owner = reflection.field<uint32_t>(component, "Owner");
    return owner != 0 && reflection.flag(component, "bAttached") && reflection.flag(component, "CollideActors") &&
           reflection.field<uint32_t>(component, "BodyInstance") == 0 && !reflection.isDefaultObject(owner);
}

uint32_t Engine::playerController()
{
    return reflection.field<uint32_t>(read<uint32_t>(reflection.field<uint32_t>(engineObject_, "GamePlayers")), "Actor");
}

uint32_t Engine::cameraPoseOffset(uint32_t camera)
{
    Property cache = reflection.property(reflection.classOf(camera), "CameraCache");
    Property pose = reflection.property(cache.typeData, "POV");
    if (reflection.property(pose.typeData, "Location").offset != 0 ||
        reflection.property(pose.typeData, "Rotation").offset != CameraRotationOffset ||
        reflection.property(pose.typeData, "FOV").offset != CameraFieldOfViewOffset)
    {
        throw UnsupportedGame("the camera structure has an unexpected layout");
    }
    return cache.offset + pose.offset;
}

namespace
{
    constexpr uint32_t MeshLodsOffset = 0x28;
    constexpr uint32_t MeshKdopTreeOffset = 0x64;
    constexpr uint32_t LodPositionsOffset = 0x4c;
    constexpr uint32_t LodCenterOffset = 0x5c;
    constexpr uint32_t LodExtentOffset = 0x68;
    constexpr uint32_t LodIndicesOffset = 0xbc;
    constexpr uint32_t ModelNodesOffset = 0x28;
    constexpr uint32_t ModelVerticesOffset = 0x38;
    constexpr uint32_t ModelPointsOffset = 0x58;
    constexpr float BrushPlaneTolerance = 1;

#pragma pack(push, 1)
    struct BspNode
    {
        Float3 normal;
        float distance;
        int32_t vertexPool;
        uint8_t unknown[34];
        uint8_t vertexCount;
        uint8_t rest[9];
    };

    struct BspVertex
    {
        int32_t point;
        uint8_t rest[20];
    };
#pragma pack(pop)
    static_assert(sizeof(BspNode) == 64 && sizeof(BspVertex) == 24);

    class EngineCollector
    {
    public:
        EngineCollector(Engine& engine, SceneBuilder& builder) : engine_(engine), reflection_(engine.reflection), builder_(builder) {}

        void collect(uint32_t object)
        {
            Handler handler = handlerOf(reflection_.classOf(object));
            if (handler == Handler::Pawn)
            {
                collectPawn(object);
            }
            else if (handler != Handler::Ignored && handler != Handler::None)
            {
                collectShape(object, handler);
            }
        }

    private:
        enum class Handler { Ignored, StaticMesh, Brush, Cylinder, Pawn, None };
        static constexpr const char* HandlerClasses[] = {"InstancedStaticMeshComponent", "StaticMeshComponent", "BrushComponent",
                                                         "CylinderComponent", "Pawn"};

        Engine& engine_;
        Reflection& reflection_;
        SceneBuilder& builder_;
        std::unordered_map<uint32_t, Handler> handlers_;
        std::unordered_map<uint32_t, Polygons> meshTriangles_;
        std::unordered_map<uint32_t, bool> pawnClasses_;

        Handler handlerOf(uint32_t objectClass)
        {
            auto known = handlers_.find(objectClass);
            if (known == handlers_.end())
            {
                Handler handler = Handler::None;
                for (int i = 0; i < static_cast<int>(std::size(HandlerClasses)) && handler == Handler::None; i++)
                {
                    if (reflection_.derives(objectClass, HandlerClasses[i]))
                    {
                        handler = static_cast<Handler>(i);
                    }
                }
                known = handlers_.emplace(objectClass, handler).first;
            }
            return known->second;
        }

        void collectPawn(uint32_t pawn)
        {
            if (reflection_.isDefaultObject(pawn))
            {
                return;
            }
            uint32_t cylinder = reflection_.field<uint32_t>(pawn, "CylinderComponent");
            if (cylinder != 0)
            {
                builder_.addCharacterCylinder(cylinder);
            }
        }

        void collectShape(uint32_t component, Handler handler)
        {
            if ((handler == Handler::Cylinder && isOwnedByPawn(component)) || !engine_.collidesWithoutPhysicsBody(component))
            {
                return;
            }
            Kind kind = engine_.nonBlockingKind(component).value_or(Kind::Engine);
            Polygons polygons = handler == Handler::StaticMesh ? staticMeshPolygons(component)
                              : handler == Handler::Brush      ? brushPolygons(component)
                                                               : cylinderPolygons(component);
            if (!polygons.empty())
            {
                builder_.add(Source::EngineComponent, kind, component, polygons, identity());
            }
        }

        bool isOwnedByPawn(uint32_t component)
        {
            uint32_t owner = reflection_.field<uint32_t>(component, "Owner");
            if (owner == 0)
            {
                return false;
            }
            uint32_t ownerClass = reflection_.classOf(owner);
            auto known = pawnClasses_.find(ownerClass);
            if (known == pawnClasses_.end())
            {
                known = pawnClasses_.emplace(ownerClass, reflection_.derives(ownerClass, "Pawn")).first;
            }
            return known->second;
        }

        Polygons cylinderPolygons(uint32_t component)
        {
            float radius = reflection_.field<float>(component, "CollisionRadius");
            float height = reflection_.field<float>(component, "CollisionHeight");
            return shapes::unitCylinder().transformBy(scaling(radius, radius, height));
        }

        Polygons brushPolygons(uint32_t component)
        {
            Property aggregate = reflection_.property(reflection_.classOf(component), "BrushAggGeom");
            Polygons polygons = aggregatePolygons(component + aggregate.offset, aggregate.typeData);
            if (!polygons.empty())
            {
                return polygons;
            }
            uint32_t model = reflection_.field<uint32_t>(component, "Brush");
            return model == 0 ? polygons : modelPolygons(model);
        }

        Polygons modelPolygons(uint32_t model)
        {
            int nodeCount = std::clamp(read<int32_t>(model + ModelNodesOffset + 4), 0, 1 << 16);
            int vertexCount = std::clamp(read<int32_t>(model + ModelVerticesOffset + 4), 0, 1 << 20);
            std::vector<BspNode> nodes = readArray<BspNode>(read<uint32_t>(model + ModelNodesOffset), nodeCount);
            std::vector<BspVertex> vertices = readArray<BspVertex>(read<uint32_t>(model + ModelVerticesOffset), vertexCount);
            std::vector<Float3> points = readCountedArray<Float3>(model + ModelPointsOffset);
            Polygons polygons;
            std::vector<Float3> polygon;
            for (const BspNode& node : nodes)
            {
                int count = node.vertexCount;
                if (node.vertexPool < 0 || count < 3 || int64_t(node.vertexPool) + count > vertexCount)
                {
                    continue;
                }
                polygon.clear();
                for (int k = 0; k < count; k++)
                {
                    uint32_t point = static_cast<uint32_t>(vertices[node.vertexPool + k].point);
                    if (point >= points.size())
                    {
                        return {};
                    }
                    const Float3& p = points[point];
                    if (fabsf(p.x * node.normal.x + p.y * node.normal.y + p.z * node.normal.z - node.distance) > BrushPlaneTolerance)
                    {
                        return {};
                    }
                    polygon.push_back(p);
                }
                polygons.add(polygon.data(), count);
            }
            return polygons;
        }

        Polygons staticMeshPolygons(uint32_t component)
        {
            uint32_t mesh = reflection_.field<uint32_t>(component, "StaticMesh");
            if (mesh == 0)
            {
                return {};
            }
            if (Polygons simple = simpleCollision(mesh); !simple.empty())
            {
                return simple;
            }
            if (read<int32_t>(mesh + MeshKdopTreeOffset + 4) <= 0)
            {
                return {};
            }
            auto known = meshTriangles_.find(mesh);
            if (known == meshTriangles_.end())
            {
                known = meshTriangles_.emplace(mesh, meshTriangles(mesh)).first;
            }
            return known->second;
        }

        Polygons simpleCollision(uint32_t mesh)
        {
            if (!reflection_.flag(mesh, "UseSimpleBoxCollision"))
            {
                return {};
            }
            uint32_t setup = reflection_.field<uint32_t>(mesh, "BodySetup");
            if (setup == 0)
            {
                return {};
            }
            Property aggregate = reflection_.property(reflection_.classOf(setup), "AggGeom");
            return aggregatePolygons(setup + aggregate.offset, aggregate.typeData);
        }

        Polygons meshTriangles(uint32_t mesh)
        {
            uint32_t lod = read<uint32_t>(read<uint32_t>(mesh + MeshLodsOffset));
            uint32_t data = read<uint32_t>(lod + LodPositionsOffset);
            int stride = read<int32_t>(lod + LodPositionsOffset + 4);
            int count = read<int32_t>(lod + LodPositionsOffset + 8);
            if ((stride != 8 && stride != 12) || count <= 0 || count > (1 << 20))
            {
                return {};
            }
            Float3 center = read<Float3>(lod + LodCenterOffset);
            Float3 extent = read<Float3>(lod + LodExtentOffset);
            std::vector<uint8_t> raw = readArray<uint8_t>(data, int64_t(stride) * count);
            std::vector<Float3> vertices(count);
            for (int i = 0; i < count; i++)
            {
                if (stride == 12)
                {
                    memcpy(&vertices[i], &raw[12 * i], 12);
                    continue;
                }
                int16_t packed[4];
                memcpy(packed, &raw[8 * i], 8);
                if (packed[3] != INT16_MAX)
                {
                    return {};
                }
                vertices[i] = {center.x + extent.x * packed[0] / INT16_MAX,
                               center.y + extent.y * packed[1] / INT16_MAX,
                               center.z + extent.z * packed[2] / INT16_MAX};
            }
            std::vector<uint16_t> indices = readCountedArray<uint16_t>(lod + LodIndicesOffset);
            Polygons triangles;
            for (size_t t = 0; t + 2 < indices.size(); t += 3)
            {
                if (indices[t] >= count || indices[t + 1] >= count || indices[t + 2] >= count)
                {
                    return {};
                }
                triangles.add({vertices[indices[t]], vertices[indices[t + 1]], vertices[indices[t + 2]]});
            }
            return triangles;
        }

        Polygons aggregatePolygons(uint32_t aggregate, uint32_t structure)
        {
            Polygons polygons;
            for (Reflection::Element element : reflection_.elements(aggregate, structure, "SphereElems"))
            {
                polygons.append(shapes::capsule(reflection_.elementField<float>(element, "Radius"), 0)
                                    .transformBy(reflection_.elementField<Matrix>(element, "TM")));
            }
            for (Reflection::Element element : reflection_.elements(aggregate, structure, "BoxElems"))
            {
                Float3 halfExtent(reflection_.elementField<float>(element, "X") / 2, reflection_.elementField<float>(element, "Y") / 2,
                                  reflection_.elementField<float>(element, "Z") / 2);
                polygons.append(shapes::box(halfExtent).transformBy(reflection_.elementField<Matrix>(element, "TM")));
            }
            for (Reflection::Element element : reflection_.elements(aggregate, structure, "SphylElems"))
            {
                Matrix alongZ = mul(store(XMMatrixRotationY(XM_PIDIV2)), reflection_.elementField<Matrix>(element, "TM"));
                polygons.append(shapes::capsule(reflection_.elementField<float>(element, "Radius"), reflection_.elementField<float>(element, "Length") / 2)
                                    .transformBy(alongZ));
            }
            for (Reflection::Element element : reflection_.elements(aggregate, structure, "ConvexElems"))
            {
                std::vector<Float3> vertices = reflection_.elementArray<Float3>(element, "VertexData");
                std::vector<int32_t> indices = reflection_.elementArray<int32_t>(element, "FaceTriData");
                for (size_t t = 0; t + 2 < indices.size(); t += 3)
                {
                    if (static_cast<uint32_t>(indices[t]) < vertices.size() && static_cast<uint32_t>(indices[t + 1]) < vertices.size() &&
                        static_cast<uint32_t>(indices[t + 2]) < vertices.size())
                    {
                        polygons.add({vertices[indices[t]], vertices[indices[t + 1]], vertices[indices[t + 2]]});
                    }
                }
            }
            return polygons;
        }
    };
}

void Engine::collect(SceneBuilder& builder)
{
    EngineCollector collector(*this, builder);
    for (uint32_t object : reflection.allObjects())
    {
        if (object == 0)
        {
            continue;
        }
        try
        {
            collector.collect(object);
        }
        catch (const std::exception& e)
        {
            report("an engine component", e);
        }
    }
}
