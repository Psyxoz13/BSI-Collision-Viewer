#pragma once

#include "model/engine.h"
#include "model/settings.h"

#include <d3d11.h>
#include <wrl/client.h>

class Renderer
{
public:
    explicit Renderer(ID3D11Device* device);

    void upload(const Scene& scene);
    void draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, const D3D11_TEXTURE2D_DESC& backBuffer,
              const Scene& scene, const CharacterSnapshot& characters, const CameraPose& camera, const Settings& settings);

private:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11VertexShader> vertexShader_;
    ComPtr<ID3D11PixelShader> pixelShader_;
    ComPtr<ID3D11InputLayout> inputLayout_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11Buffer> faces_;
    ComPtr<ID3D11Buffer> edges_;
    ComPtr<ID3D11BlendState> premultiplied_;
    ComPtr<ID3D11BlendState> depthOnly_;
    ComPtr<ID3D11DepthStencilState> depthWrite_;
    ComPtr<ID3D11DepthStencilState> depthTest_;
    ComPtr<ID3D11DepthStencilState> depthOff_;
    ComPtr<ID3D11RasterizerState> biased_;
    ComPtr<ID3D11RasterizerState> unbiased_;
    ComPtr<ID3D11DepthStencilView> depth_;
    D3D11_TEXTURE2D_DESC depthSize_ = {};
};
