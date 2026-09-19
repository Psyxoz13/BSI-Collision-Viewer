#include "collision.h"

#include <cmath>
#include <set>

using namespace DirectX;

const char* const KindLabels[KindCount] = {
    "Triangle meshes",
    "Convex hulls",
    "Boxes / capsules",
    "Moving objects",
    "Engine collision (doors, movers)",
    "Characters",
    "Inactive colliders (no player interaction)",
    "Triggers",
    "Blocks bullets only (you walk through)",
};

void Polygons::add(const Float3* polygon, int count)
{
    points.insert(points.end(), polygon, polygon + count);
    sizes.push_back(count);
}

void Polygons::append(const Polygons& other)
{
    points.insert(points.end(), other.points.begin(), other.points.end());
    sizes.insert(sizes.end(), other.sizes.begin(), other.sizes.end());
}

Polygons& Polygons::transformBy(const Matrix& m)
{
    for (Float3& point : points)
    {
        point = transform(point, m);
    }
    return *this;
}

namespace shapes
{
    Polygons box(const Float3& h)
    {
        static constexpr int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
        Float3 corners[8];
        for (int i = 0; i < 8; i++)
        {
            corners[i] = {(i & 4) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 1) ? h.z : -h.z};
        }
        Polygons polygons;
        for (const auto& face : faces)
        {
            polygons.add({corners[face[0]], corners[face[1]], corners[face[2]], corners[face[3]]});
        }
        return polygons;
    }

    Polygons capsule(float radius, float halfLength)
    {
        constexpr int Segments = 12;
        constexpr int RingsPerHemisphere = 4;
        struct Ring
        {
            float axial;
            float radius;
        };
        std::vector<Ring> rings;
        auto addRing = [&](float center, float latitude) { rings.push_back({center + radius * sinf(latitude), radius * cosf(latitude)}); };
        for (int i = 0; i <= RingsPerHemisphere; i++)
        {
            addRing(-halfLength, -XM_PIDIV2 + XM_PIDIV2 * i / RingsPerHemisphere);
        }
        for (int i = 0; i <= RingsPerHemisphere; i++)
        {
            addRing(halfLength, XM_PIDIV2 * i / RingsPerHemisphere);
        }
        auto point = [](const Ring& ring, int segment)
        {
            float angle = XM_2PI * segment / Segments;
            return Float3(ring.axial, ring.radius * cosf(angle), ring.radius * sinf(angle));
        };
        Polygons polygons;
        for (size_t ring = 0; ring + 1 < rings.size(); ring++)
        {
            for (int segment = 0; segment < Segments; segment++)
            {
                polygons.add({point(rings[ring], segment), point(rings[ring], segment + 1),
                              point(rings[ring + 1], segment + 1), point(rings[ring + 1], segment)});
            }
        }
        return polygons;
    }

    Polygons unitCylinder()
    {
        constexpr int Segments = 16;
        auto point = [](int segment, float height)
        {
            float angle = XM_2PI * segment / Segments;
            return Float3(cosf(angle), sinf(angle), height);
        };
        Polygons polygons;
        Float3 top[Segments];
        Float3 bottom[Segments];
        for (int j = 0; j < Segments; j++)
        {
            polygons.add({point(j, -1), point(j + 1, -1), point(j + 1, 1), point(j, 1)});
            top[j] = point(j, 1);
            bottom[j] = point(Segments - j, -1);
        }
        polygons.add(top, Segments);
        polygons.add(bottom, Segments);
        return polygons;
    }
}

std::span<const Matrix> placementsOf(const Group& group, const CharacterSnapshot& characters)
{
    switch (group.source)
    {
    case Source::Static:
        return {&group.placement, 1};
    case Source::Character:
        return characters.transforms[static_cast<int>(group.kind)];
    default:
        return group.isTracked ? std::span<const Matrix>(&group.placement, 1) : std::span<const Matrix>();
    }
}

int Scene::trackedObjectCount() const
{
    std::set<uint32_t> targets;
    for (const Group& group : groups)
    {
        if (group.isTracked)
        {
            targets.insert(group.target);
        }
    }
    return static_cast<int>(targets.size());
}

bool Scene::hasLostTracking() const
{
    return std::any_of(groups.begin(), groups.end(), [](const Group& group) { return group.lostTracking; });
}

namespace
{
    float shade(const Float3& a, const Float3& b, const Float3& c)
    {
        static const XMVECTOR light = XMVector3Normalize(XMVectorSet(0.3f, 0.5f, 0.81f, 0));
        XMVECTOR origin = XMLoadFloat3(&a);
        XMVECTOR normal = XMVector3Cross(XMLoadFloat3(&b) - origin, XMLoadFloat3(&c) - origin);
        float length = XMVectorGetX(XMVector3Length(normal));
        return length > 0 ? 0.55f + 0.45f * fabsf(XMVectorGetX(XMVector3Dot(normal / length, light))) : 0.55f;
    }
}

void SceneBuilder::add(Source source, Kind kind, uint32_t target, const Polygons& polygons, const Matrix& transform)
{
    auto [entry, inserted] = meshIndex_.try_emplace({source, kind, target}, meshes_.size());
    if (inserted)
    {
        meshes_.push_back({Group{source, kind, target}});
    }
    Mesh& mesh = meshes_[entry->second];
    std::vector<Float3> points;
    size_t offset = 0;
    for (int size : polygons.sizes)
    {
        points.clear();
        for (int i = 0; i < size; i++)
        {
            points.push_back(::transform(polygons.points[offset + i], transform));
        }
        offset += size;
        for (int i = 1; i + 1 < size; i++)
        {
            float faceShade = shade(points[0], points[i], points[i + 1]);
            mesh.faces.insert(mesh.faces.end(), {{points[0], faceShade}, {points[i], faceShade}, {points[i + 1], faceShade}});
        }
        for (int i = 0; i < size; i++)
        {
            mesh.edges.insert(mesh.edges.end(), {{points[i], 1}, {points[(i + 1) % size], 1}});
        }
    }
}

Scene SceneBuilder::build()
{
    std::stable_sort(meshes_.begin(), meshes_.end(), [](const Mesh& a, const Mesh& b) { return a.group.kind < b.group.kind; });
    Scene scene;
    for (Mesh& mesh : meshes_)
    {
        mesh.group.faces = {static_cast<int>(scene.faces.size()), static_cast<int>(mesh.faces.size())};
        mesh.group.edges = {static_cast<int>(scene.edges.size()), static_cast<int>(mesh.edges.size())};
        scene.faces.insert(scene.faces.end(), mesh.faces.begin(), mesh.faces.end());
        scene.edges.insert(scene.edges.end(), mesh.edges.begin(), mesh.edges.end());
        scene.groups.push_back(mesh.group);
    }
    scene.characterCylinders = cylinders_;
    meshIndex_.clear();
    meshes_.clear();
    return scene;
}
