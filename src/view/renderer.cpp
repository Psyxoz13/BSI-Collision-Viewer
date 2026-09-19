#include "view/renderer.h"

#include <d3dcompiler.h>

#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    constexpr char ShaderSource[] = R"(
cbuffer Constants : register(b0)
{
    row_major float4x4 model;
    row_major float4x4 viewProjection;
    float3 color;
    float alpha;
    float3 camera;
    float2 fade;
};

struct Pixel
{
    float4 position : SV_Position;
    float shade : SHADE;
    float3 world : WORLD;
};

Pixel vs(float3 position : POSITION, float shade : SHADE)
{
    Pixel pixel;
    float4 world = mul(float4(position, 1), model);
    pixel.position = mul(world, viewProjection);
    pixel.shade = shade;
    pixel.world = world.xyz;
    return pixel;
}

float4 ps(Pixel pixel) : SV_Target
{
    float a = alpha;
    if (fade.y > 0)
        a *= 1 - saturate((distance(pixel.world, camera) - fade.x) / max(fade.y - fade.x, 0.001));
    return float4(color * pixel.shade * a, a);
}
)";

    struct Constants
    {
        Matrix model;
        Matrix viewProjection;
        Float3 color;
        float alpha;
        Float3 camera;
        float padding;
        XMFLOAT2 fade;
        float padding2[2];
    };
    static_assert(sizeof(Constants) == 176);

    ComPtr<ID3DBlob> compile(const char* entryPoint, const char* target)
    {
        ComPtr<ID3DBlob> code, errors;
        if (FAILED(D3DCompile(ShaderSource, sizeof ShaderSource - 1, "collision", nullptr, nullptr, entryPoint, target, 0, 0, &code, &errors)))
        {
            throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "the collision shader did not compile");
        }
        return code;
    }

    ComPtr<ID3D11Buffer> vertexBuffer(ID3D11Device* device, const std::vector<Vertex>& vertices)
    {
        ComPtr<ID3D11Buffer> buffer;
        if (vertices.empty())
        {
            return buffer;
        }
        D3D11_BUFFER_DESC desc = {static_cast<UINT>(vertices.size() * sizeof(Vertex)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER};
        D3D11_SUBRESOURCE_DATA data = {vertices.data()};
        check(device->CreateBuffer(&desc, &data, &buffer), "creating the collision vertex buffer");
        return buffer;
    }
}

Renderer::Renderer(ID3D11Device* device) : device_(device)
{
    ComPtr<ID3DBlob> vertexCode = compile("vs", "vs_4_0");
    ComPtr<ID3DBlob> pixelCode = compile("ps", "ps_4_0");
    check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertexShader_), "creating the vertex shader");
    check(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &pixelShader_), "creating the pixel shader");
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"SHADE", 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    check(device->CreateInputLayout(layout, 2, vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), &inputLayout_), "creating the input layout");

    D3D11_BUFFER_DESC constants = {sizeof(Constants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE};
    check(device->CreateBuffer(&constants, nullptr, &constants_), "creating the constant buffer");

    CD3D11_BLEND_DESC blend(D3D11_DEFAULT);
    blend.RenderTarget[0].RenderTargetWriteMask = 0;
    check(device->CreateBlendState(&blend, &depthOnly_), "creating a blend state");
    blend.RenderTarget[0] = {TRUE, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD,
                             D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
    check(device->CreateBlendState(&blend, &premultiplied_), "creating a blend state");

    CD3D11_DEPTH_STENCIL_DESC depth(D3D11_DEFAULT);
    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    check(device->CreateDepthStencilState(&depth, &depthWrite_), "creating a depth state");
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    check(device->CreateDepthStencilState(&depth, &depthTest_), "creating a depth state");
    depth.DepthEnable = FALSE;
    check(device->CreateDepthStencilState(&depth, &depthOff_), "creating a depth state");

    CD3D11_RASTERIZER_DESC rasterizer(D3D11_DEFAULT);
    rasterizer.CullMode = D3D11_CULL_NONE;
    check(device->CreateRasterizerState(&rasterizer, &unbiased_), "creating a rasterizer state");
    rasterizer.DepthBias = 1;
    rasterizer.SlopeScaledDepthBias = 1;
    check(device->CreateRasterizerState(&rasterizer, &biased_), "creating a rasterizer state");
}

void Renderer::upload(const Scene& scene)
{
    ComPtr<ID3D11Buffer> faces = vertexBuffer(device_.Get(), scene.faces);
    ComPtr<ID3D11Buffer> edges = vertexBuffer(device_.Get(), scene.edges);
    faces_ = faces;
    edges_ = edges;
}

void Renderer::draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, const D3D11_TEXTURE2D_DESC& backBuffer,
                    const Scene& scene, const CharacterSnapshot& characters, const CameraPose& camera, const Settings& settings)
{
    if (!depth_ || depthSize_.Width != backBuffer.Width || depthSize_.Height != backBuffer.Height ||
        depthSize_.SampleDesc.Count != backBuffer.SampleDesc.Count || depthSize_.SampleDesc.Quality != backBuffer.SampleDesc.Quality)
    {
        D3D11_TEXTURE2D_DESC desc = {backBuffer.Width, backBuffer.Height, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, backBuffer.SampleDesc,
                                     D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL};
        ComPtr<ID3D11Texture2D> texture;
        depth_.Reset();
        check(device_->CreateTexture2D(&desc, nullptr, &texture), "creating the depth buffer");
        check(device_->CreateDepthStencilView(texture.Get(), nullptr, &depth_), "creating the depth buffer view");
        depthSize_ = desc;
    }
    context->ClearDepthStencilView(depth_.Get(), D3D11_CLEAR_DEPTH, 1, 0);
    context->OMSetRenderTargets(1, &target, depth_.Get());
    D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(backBuffer.Width), static_cast<float>(backBuffer.Height), 0, 1};
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(inputLayout_.Get());
    context->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context->PSSetShader(pixelShader_.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, 1, constants_.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());

    bool fade = settings.occlusion == Occlusion::DistanceFade;
    float aspect = static_cast<float>(backBuffer.Width) / std::max(backBuffer.Height, 1u);
    Constants constants = {};
    constants.viewProjection = camera.viewProjection(aspect, settings.fovScale, fade ? settings.fadeEnd : settings.drawDistance);
    constants.camera = {camera.location.x * MetersPerUnit, camera.location.y * MetersPerUnit, camera.location.z * MetersPerUnit};
    constants.fade = fade ? XMFLOAT2(settings.fadeStart, settings.fadeEnd) : XMFLOAT2(0, 0);

    auto drawGroups = [&](ID3D11Buffer* mesh, D3D11_PRIMITIVE_TOPOLOGY topology, MeshRange Group::* range, float opacity, auto includes)
    {
        if (!mesh)
        {
            return;
        }
        UINT stride = sizeof(Vertex), offset = 0;
        context->IASetVertexBuffers(0, 1, &mesh, &stride, &offset);
        context->IASetPrimitiveTopology(topology);
        constants.alpha = opacity;
        for (const Group& group : scene.groups)
        {
            if (!includes(group.kind) || (group.*range).count == 0)
            {
                continue;
            }
            constants.color = settings.colors[static_cast<int>(group.kind)];
            for (const Matrix& placement : placementsOf(group, characters))
            {
                constants.model = placement;
                D3D11_MAPPED_SUBRESOURCE mapped;
                check(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "updating the constant buffer");
                memcpy(mapped.pData, &constants, sizeof constants);
                context->Unmap(constants_.Get(), 0);
                context->Draw((group.*range).count, (group.*range).start);
            }
        }
    };
    auto isVisible = [&](Kind kind) { return settings.isVisible(kind); };

    if (settings.occlusion == Occlusion::NearestCollider)
    {
        context->OMSetDepthStencilState(depthWrite_.Get(), 0);
        context->OMSetBlendState(depthOnly_.Get(), nullptr, 0xffffffff);
        context->RSSetState(biased_.Get());
        drawGroups(faces_.Get(), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, &Group::faces, 1.f, [&](Kind kind) { return settings.isOccluder(kind); });
        context->OMSetDepthStencilState(depthTest_.Get(), 0);
    }
    else
    {
        context->OMSetDepthStencilState(depthOff_.Get(), 0);
    }
    context->OMSetBlendState(premultiplied_.Get(), nullptr, 0xffffffff);
    if (settings.drawFaces)
    {
        context->RSSetState(biased_.Get());
        drawGroups(faces_.Get(), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, &Group::faces, settings.faceOpacity, isVisible);
    }
    if (settings.drawEdges)
    {
        context->RSSetState(unbiased_.Get());
        drawGroups(edges_.Get(), D3D11_PRIMITIVE_TOPOLOGY_LINELIST, &Group::edges, settings.edgeOpacity, isVisible);
    }
}
