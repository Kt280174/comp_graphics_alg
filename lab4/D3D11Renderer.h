#pragma once
#include "Common.h"
#include "Camera.h"
#include "TextureLoader.h"

class D3D11Renderer
{
private:
    HWND m_hWnd;
    UINT m_width;
    UINT m_height;

    ID3D11Device* m_pDevice;
    ID3D11DeviceContext* m_pContext;
    IDXGISwapChain* m_pSwapChain;
    ID3D11RenderTargetView* m_pBackBufferRTV;
    ID3D11DepthStencilView* m_pDepthStencilView;

    ID3D11Buffer* m_pVertexBuffer;
    ID3D11Buffer* m_pIndexBuffer;
    ID3D11VertexShader* m_pVertexShader;
    ID3D11PixelShader* m_pPixelShader;
    ID3D11InputLayout* m_pInputLayout;
    ID3D11Buffer* m_pModelCB;

    ID3D11Buffer* m_pSkyboxVertexBuffer;
    ID3D11Buffer* m_pSkyboxIndexBuffer;
    ID3D11VertexShader* m_pSkyboxVS;
    ID3D11PixelShader* m_pSkyboxPS;
    ID3D11InputLayout* m_pSkyboxInputLayout;

    ID3D11Buffer* m_pViewProjCB;
    ID3D11ShaderResourceView* m_pTextureView;
    ID3D11ShaderResourceView* m_pCubemapView;
    ID3D11SamplerState* m_pSampler;

    Camera m_camera;
    double m_lastFrameTime;

    bool m_keyLeft, m_keyRight, m_keyUp, m_keyDown;

public:
    D3D11Renderer();
    ~D3D11Renderer();

    bool Initialize(HWND hWnd, UINT width, UINT height);
    void Cleanup();
    void Render();
    void Resize(UINT newWidth, UINT newHeight);
    void HandleKey(UINT key, bool isDown);

private:
    bool CreateDeviceAndSwapChain();
    bool CreateRenderTargetAndDepthStencil();
    bool CreateBuffers();
    bool CompileShaders();
    bool LoadTextures();
    void UpdateCamera(float deltaTime);
    void RenderSkybox(const XMMATRIX& vpSky);
    void RenderCube(const XMMATRIX& view, const XMMATRIX& proj, float time);
};