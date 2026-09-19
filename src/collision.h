#pragma once

#include "common.h"

#include <DirectXMath.h>

#include <array>
#include <initializer_list>
#include <map>
#include <span>
#include <tuple>

using Float3 = DirectX::XMFLOAT3;
using Matrix = DirectX::XMFLOAT4X4;

inline DirectX::XMMATRIX load(const Matrix& m) { return DirectX::XMLoadFloat4x4(&m); }
inline Matrix store(DirectX::FXMMATRIX m) { Matrix result; DirectX::XMStoreFloat4x4(&result, m); return result; }
inline Matrix mul(const Matrix& a, const Matrix& b) { return store(DirectX::XMMatrixMultiply(load(a), load(b))); }
inline Matrix identity() { return store(DirectX::XMMatrixIdentity()); }
inline Matrix scaling(float x, float y, float z) { return store(DirectX::XMMatrixScaling(x, y, z)); }
inline Matrix translation(const Float3& t) { return store(DirectX::XMMatrixTranslation(t.x, t.y, t.z)); }

inline Float3 transform(const Float3& point, const Matrix& m)
{
    Float3 result;
    DirectX::XMStoreFloat3(&result, DirectX::XMVector3Transform(DirectX::XMLoadFloat3(&point), load(m)));
    return result;
}

enum class Kind { TriangleMesh, ConvexHull, Primitive, Dynamic, Engine, Character, Inactive, Trigger, BulletsOnly, Count };
constexpr int KindCount = static_cast<int>(Kind::Count);
extern const char* const KindLabels[KindCount];

struct Polygons
{
    std::vector<Float3> points;
    std::vector<int> sizes;

    void add(const Float3* polygon, int count);
    void add(std::initializer_list<Float3> polygon) { add(polygon.begin(), static_cast<int>(polygon.size())); }
    void append(const Polygons& other);
    bool empty() const { return sizes.empty(); }
    Polygons& transformBy(const Matrix& m);
};

namespace shapes
{
    Polygons box(const Float3& halfExtent);
    Polygons capsule(float radius, float halfLength);
    Polygons unitCylinder();
}

struct Vertex
{
    Float3 position;
    float shade;
};

struct MeshRange
{
    int start = 0;
    int count = 0;
    bool operator==(const MeshRange&) const = default;
};

enum class Source { Static, Character, PhysXActor, RigidBody, EngineComponent };

struct Group
{
    Source source;
    Kind kind;
    uint32_t target;
    MeshRange faces;
    MeshRange edges;
    bool isTracked = false;
    bool lostTracking = false;
    Matrix placement = identity();
};

struct CharacterSnapshot
{
    std::array<std::vector<Matrix>, KindCount> transforms;
    int count = 0;
};

std::span<const Matrix> placementsOf(const Group& group, const CharacterSnapshot& characters);

struct Scene
{
    std::vector<Group> groups;
    std::vector<uint32_t> characterCylinders;
    std::vector<Vertex> faces;
    std::vector<Vertex> edges;

    int triangleCount() const { return static_cast<int>(faces.size() / 3); }
    int trackedObjectCount() const;
    bool hasLostTracking() const;

    template <class ReadPlacement>
    void track(ReadPlacement readPlacement)
    {
        for (Group& group : groups)
        {
            if (group.source == Source::Static || group.source == Source::Character)
            {
                continue;
            }
            try
            {
                group.placement = readPlacement(group);
                group.lostTracking = false;
                group.isTracked = true;
            }
            catch (const std::exception& e)
            {
                report("a moving group", e);
                group.lostTracking = group.isTracked;
                group.isTracked = false;
            }
        }
    }
};

class SceneBuilder
{
public:
    void add(Source source, Kind kind, uint32_t target, const Polygons& polygons, const Matrix& transform);
    void addCharacterCylinder(uint32_t cylinder) { cylinders_.push_back(cylinder); }
    Scene build();

private:
    struct Mesh
    {
        Group group;
        std::vector<Vertex> faces;
        std::vector<Vertex> edges;
    };

    std::map<std::tuple<Source, Kind, uint32_t>, size_t> meshIndex_;
    std::vector<Mesh> meshes_;
    std::vector<uint32_t> cylinders_;
};
