#include "D3D11Renderer.h"

D3D11Renderer::D3D11Renderer()
    : m_hWnd(nullptr), m_width(1280), m_height(720), m_lastFrameTime(0),
    m_keyLeft(false), m_keyRight(false), m_keyUp(false), m_keyDown(false),
    m_pDevice(nullptr), m_pContext(nullptr), m_pSwapChain(nullptr),
    m_pBackBufferRTV(nullptr), m_pDepthStencilView(nullptr),
    m_pVertexBuffer(nullptr), m_pIndexBuffer(nullptr),
    m_pVertexShader(nullptr), m_pPixelShader(nullptr),
    m_pInputLayout(nullptr), m_pModelCB(nullptr),
    m_pSkyboxVertexBuffer(nullptr), m_pSkyboxIndexBuffer(nullptr),
    m_pSkyboxVS(nullptr), m_pSkyboxPS(nullptr), m_pSkyboxInputLayout(nullptr),
    m_pViewProjCB(nullptr), m_pTextureView(nullptr),
    m_pCubemapView(nullptr), m_pSampler(nullptr)
{
    m_lastFrameTime = (double)GetTickCount64() / 1000.0;
}

D3D11Renderer::~D3D11Renderer()
{
    Cleanup();
}

bool D3D11Renderer::Initialize(HWND hWnd, UINT width, UINT height)
{
    m_hWnd = hWnd;
    m_width = width;
    m_height = height;

    if (!CreateDeviceAndSwapChain()) return false;
    if (!CreateRenderTargetAndDepthStencil()) return false;
    if (!CreateBuffers()) return false;
    if (!CompileShaders()) return false;
    if (!LoadTextures()) return false;

    return true;
}

void D3D11Renderer::Cleanup()
{
    if (m_pContext)
        m_pContext->ClearState();

    SAFE_RELEASE(m_pModelCB);
    SAFE_RELEASE(m_pViewProjCB);
    SAFE_RELEASE(m_pInputLayout);
    SAFE_RELEASE(m_pVertexShader);
    SAFE_RELEASE(m_pPixelShader);
    SAFE_RELEASE(m_pSkyboxInputLayout);
    SAFE_RELEASE(m_pSkyboxVS);
    SAFE_RELEASE(m_pSkyboxPS);
    SAFE_RELEASE(m_pIndexBuffer);
    SAFE_RELEASE(m_pVertexBuffer);
    SAFE_RELEASE(m_pSkyboxIndexBuffer);
    SAFE_RELEASE(m_pSkyboxVertexBuffer);
    SAFE_RELEASE(m_pBackBufferRTV);
    SAFE_RELEASE(m_pDepthStencilView);
    SAFE_RELEASE(m_pSwapChain);
    SAFE_RELEASE(m_pTextureView);
    SAFE_RELEASE(m_pCubemapView);
    SAFE_RELEASE(m_pSampler);

#ifdef _DEBUG
    if (m_pDevice)
    {
        ID3D11Debug* pDebug = nullptr;
        if (SUCCEEDED(m_pDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug)))
        {
            pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            pDebug->Release();
        }
    }
#endif

    SAFE_RELEASE(m_pContext);
    SAFE_RELEASE(m_pDevice);
}

bool D3D11Renderer::CreateDeviceAndSwapChain()
{
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = m_width;
    scd.BufferDesc.Height = m_height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = m_hWnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtainedLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        flags, levels, 1, D3D11_SDK_VERSION,
        &scd, &m_pSwapChain, &m_pDevice, &obtainedLevel, &m_pContext
    );

    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreateRenderTargetAndDepthStencil()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) return false;

    hr = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pBackBufferRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = m_width;
    depthDesc.Height = m_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencil = nullptr;
    hr = m_pDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (FAILED(hr)) return false;

    hr = m_pDevice->CreateDepthStencilView(pDepthStencil, nullptr, &m_pDepthStencilView);
    pDepthStencil->Release();

    return SUCCEEDED(hr);
}

bool D3D11Renderer::CreateBuffers()
{
    const TexturedVertex cubeVertices[] = {
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT2(0.0f, 0.0f) }
    };

    const USHORT cubeIndices[] = {
        0,2,1, 0,3,2, 4,5,6, 4,6,7, 8,10,9, 8,11,10,
        12,14,13, 12,15,14, 16,18,17, 16,19,18, 20,22,21, 20,23,22
    };

    const TexturedVertex skyboxVertices[] = {
        { XMFLOAT3(-10, -10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(10, -10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(10,  10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(-10,  10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(-10, -10,  10), XMFLOAT2(0,0) },
        { XMFLOAT3(10, -10,  10), XMFLOAT2(0,0) },
        { XMFLOAT3(10,  10,  10), XMFLOAT2(0,0) },
        { XMFLOAT3(-10,  10,  10), XMFLOAT2(0,0) }
    };

    const USHORT skyboxIndices[] = {
        0,2,1, 0,3,2, 4,5,6, 4,6,7, 0,7,3, 0,4,7,
        1,2,6, 1,6,5, 3,7,6, 3,6,2, 0,1,5, 0,5,4
    };

    D3D11_BUFFER_DESC desc = {};
    D3D11_SUBRESOURCE_DATA data = {};

    
    desc.ByteWidth = sizeof(cubeVertices);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem = cubeVertices;
    if (FAILED(m_pDevice->CreateBuffer(&desc, &data, &m_pVertexBuffer)))
        return false;

    desc.ByteWidth = sizeof(cubeIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = cubeIndices;
    if (FAILED(m_pDevice->CreateBuffer(&desc, &data, &m_pIndexBuffer)))
        return false;

    desc.ByteWidth = sizeof(skyboxVertices);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem = skyboxVertices;
    if (FAILED(m_pDevice->CreateBuffer(&desc, &data, &m_pSkyboxVertexBuffer)))
        return false;

    desc.ByteWidth = sizeof(skyboxIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = skyboxIndices;
    if (FAILED(m_pDevice->CreateBuffer(&desc, &data, &m_pSkyboxIndexBuffer)))
        return false;

    desc = {};
    desc.ByteWidth = sizeof(ModelConstantBuffer);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(m_pDevice->CreateBuffer(&desc, nullptr, &m_pModelCB)))
        return false;

    desc.ByteWidth = sizeof(ViewProjConstantBuffer);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_pDevice->CreateBuffer(&desc, nullptr, &m_pViewProjCB)))
        return false;

    return true;
}

bool D3D11Renderer::CompileShaders()
{
    const char* cubeVS = R"(
        cbuffer ModelCB : register(b0) { float4x4 model; }
        cbuffer ViewProjCB : register(b1) { float4x4 vp; }
        struct VSInput {
            float3 pos : POSITION;
            float2 uv : TEXCOORD;
        };
        struct VSOutput {
            float4 pos : SV_Position;
            float2 uv : TEXCOORD;
        };
        VSOutput vs(VSInput v) {
            VSOutput o;
            float4 worldPos = mul(float4(v.pos, 1.0), model);
            o.pos = mul(worldPos, vp);
            o.uv = v.uv;
            return o;
        }
    )";

    const char* cubePS = R"(
        Texture2D colorTexture : register(t0);
        SamplerState colorSampler : register(s0);
        struct VSOutput {
            float4 pos : SV_Position;
            float2 uv : TEXCOORD;
        };
        float4 ps(VSOutput p) : SV_Target0 {
            return colorTexture.Sample(colorSampler, p.uv);
        }
    )";

    const char* skyboxVS = R"(
        cbuffer ViewProjCB : register(b1) {
            float4x4 vp;
        }
        struct VSInput {
            float3 pos : POSITION;
            float2 uv : TEXCOORD;
        };
        struct VSOutput {
            float4 pos : SV_Position;
            float3 localPos : TEXCOORD;
        };
        VSOutput vs(VSInput v) {
            VSOutput o;
            o.pos = mul(float4(v.pos, 1.0), vp);
            o.localPos = v.pos;
            return o;
        }
    )";

    const char* skyboxPS = R"(
        TextureCube skyboxTexture : register(t1);
        SamplerState skyboxSampler : register(s1);
        struct VSOutput {
            float4 pos : SV_Position;
            float3 localPos : TEXCOORD;
        };
        float4 ps(VSOutput p) : SV_Target0 {
            return skyboxTexture.Sample(skyboxSampler, p.localPos);
        }
    )";

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pVsBlob = nullptr, * pPsBlob = nullptr, * pErrorBlob = nullptr;

    // Cube shaders
    if (FAILED(D3DCompile(cubeVS, strlen(cubeVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob)))
        return false;
    m_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &m_pVertexShader);

    D3DCompile(cubePS, strlen(cubePS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    m_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &m_pPixelShader);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    m_pDevice->CreateInputLayout(layout, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &m_pInputLayout);
    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);

    // Skybox shaders
    D3DCompile(skyboxVS, strlen(skyboxVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    m_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &m_pSkyboxVS);

    D3DCompile(skyboxPS, strlen(skyboxPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    m_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &m_pSkyboxPS);

    m_pDevice->CreateInputLayout(layout, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &m_pSkyboxInputLayout);

    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);

    return true;
}

bool D3D11Renderer::LoadTextures()
{
    
    TextureDesc texDesc;
    std::wstring fullPath = GetPath() + L"..\\..\\texture\\wood02.dds";
    if (!TextureLoader::LoadDDS(fullPath.c_str(), texDesc))
    {
        MessageBoxA(NULL, "Failed to load wood02.dds", "Error", MB_OK);
        return false;
    }

    m_pTextureView = TextureLoader::CreateTexture2D(m_pDevice, texDesc);
    free(texDesc.pData);

    if (!m_pTextureView)
    {
        MessageBoxA(NULL, "Failed to create texture SRV", "Error", MB_OK);
        return false;
    }

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = (FLOAT)texDesc.mipmapsCount;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    if (FAILED(m_pDevice->CreateSamplerState(&sampDesc, &m_pSampler)))
    {
        MessageBoxA(NULL, "Failed to create sampler", "Error", MB_OK);
        return false;
    }

    std::wstring path = GetPath() + L"..\\..\\texture\\skybox\\";
    std::wstring faceNames[6] = {
        path + L"posx.dds", path + L"negx.dds",
        path + L"posy.dds", path + L"negy.dds",
        path + L"posz.dds", path + L"negz.dds"
    };

    m_pCubemapView = TextureLoader::CreateCubemap(m_pDevice, faceNames);
    if (!m_pCubemapView)
    {
        MessageBoxA(NULL, "Failed to load cubemap", "Error", MB_OK);
        return false;
    }

    MessageBoxA(NULL, "Textures loaded successfully!", "Success", MB_OK);
    return true;
}

void D3D11Renderer::UpdateCamera(float deltaTime)
{
    m_camera.Update(deltaTime, m_keyLeft, m_keyRight, m_keyUp, m_keyDown);
}

void D3D11Renderer::RenderSkybox(const XMMATRIX& vpSky)
{
    
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dsDesc.StencilEnable = FALSE;
    ID3D11DepthStencilState* pDSSky = nullptr;
    m_pDevice->CreateDepthStencilState(&dsDesc, &pDSSky);
    m_pContext->OMSetDepthStencilState(pDSSky, 0);

    
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    ID3D11RasterizerState* pRSSky = nullptr;
    m_pDevice->CreateRasterizerState(&rsDesc, &pRSSky);
    m_pContext->RSSetState(pRSSky);

    m_pContext->VSSetShader(m_pSkyboxVS, nullptr, 0);
    m_pContext->PSSetShader(m_pSkyboxPS, nullptr, 0);
    m_pContext->IASetInputLayout(m_pSkyboxInputLayout);

    UINT stride = sizeof(TexturedVertex);
    UINT offset = 0;
    ID3D11Buffer* vbSky[] = { m_pSkyboxVertexBuffer };
    m_pContext->IASetVertexBuffers(0, 1, vbSky, &stride, &offset);
    m_pContext->IASetIndexBuffer(m_pSkyboxIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11Buffer* cbsSky[] = { nullptr, m_pViewProjCB };
    m_pContext->VSSetConstantBuffers(0, 2, cbsSky);

    ID3D11ShaderResourceView* skySRV[] = { m_pCubemapView };
    m_pContext->PSSetShaderResources(1, 1, skySRV);
    ID3D11SamplerState* samplers[] = { m_pSampler };
    m_pContext->PSSetSamplers(1, 1, samplers);

    
    m_pContext->DrawIndexed(36, 0, 0);

    SAFE_RELEASE(pRSSky);
    SAFE_RELEASE(pDSSky);
}

void D3D11Renderer::RenderCube(const XMMATRIX& view, const XMMATRIX& proj, float time)
{
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
    dsDesc.StencilEnable = FALSE;
    ID3D11DepthStencilState* pDSCube = nullptr;
    m_pDevice->CreateDepthStencilState(&dsDesc, &pDSCube);
    m_pContext->OMSetDepthStencilState(pDSCube, 0);

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.FrontCounterClockwise = FALSE;
    ID3D11RasterizerState* pRSCube = nullptr;
    m_pDevice->CreateRasterizerState(&rsDesc, &pRSCube);
    m_pContext->RSSetState(pRSCube);

    float angle = time * 0.5f;
    XMMATRIX model = XMMatrixRotationY(angle);
    XMMATRIX vp = view * proj;

    ModelConstantBuffer modelData;
    XMStoreFloat4x4((XMFLOAT4X4*)&modelData.model, XMMatrixTranspose(model));
    m_pContext->UpdateSubresource(m_pModelCB, 0, nullptr, &modelData, 0, 0);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_pContext->Map(m_pViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        ViewProjConstantBuffer* pData = (ViewProjConstantBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(vp));
        m_pContext->Unmap(m_pViewProjCB, 0);
    }

    m_pContext->VSSetShader(m_pVertexShader, nullptr, 0);
    m_pContext->PSSetShader(m_pPixelShader, nullptr, 0);
    m_pContext->IASetInputLayout(m_pInputLayout);

    UINT stride = sizeof(TexturedVertex);
    UINT offset = 0;
    ID3D11Buffer* vbCube[] = { m_pVertexBuffer };
    m_pContext->IASetVertexBuffers(0, 1, vbCube, &stride, &offset);
    m_pContext->IASetIndexBuffer(m_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);

    ID3D11Buffer* cbsCube[] = { m_pModelCB, m_pViewProjCB };
    m_pContext->VSSetConstantBuffers(0, 2, cbsCube);

    ID3D11ShaderResourceView* cubeSRV[] = { m_pTextureView };
    m_pContext->PSSetShaderResources(0, 1, cubeSRV);
    ID3D11SamplerState* samplers[] = { m_pSampler };
    m_pContext->PSSetSamplers(0, 1, samplers);

    m_pContext->DrawIndexed(36, 0, 0);

    SAFE_RELEASE(pRSCube);
    SAFE_RELEASE(pDSCube);
}

void D3D11Renderer::Render()
{
    if (!m_pContext || !m_pBackBufferRTV || !m_pSwapChain)
        return;

    double currentTime = (double)GetTickCount64() / 1000.0;
    float deltaTime = (float)(currentTime - m_lastFrameTime);
    m_lastFrameTime = currentTime;

    UpdateCamera(deltaTime);

    m_pContext->ClearState();
    m_pContext->OMSetRenderTargets(1, &m_pBackBufferRTV, m_pDepthStencilView);
    const float clearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    m_pContext->ClearRenderTargetView(m_pBackBufferRTV, clearColor);
    m_pContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT vpView = { 0, 0, (float)m_width, (float)m_height, 0.0f, 1.0f };
    m_pContext->RSSetViewports(1, &vpView);

    XMMATRIX view = m_camera.GetViewMatrix();
    XMMATRIX viewNoTrans = m_camera.GetViewNoTranslationMatrix();
    float aspect = (float)m_width / (float)m_height;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3.0f, aspect, 0.1f, 100.0f);
    XMMATRIX vpSky = viewNoTrans * proj;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_pContext->Map(m_pViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        ViewProjConstantBuffer* pData = (ViewProjConstantBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(vpSky));
        m_pContext->Unmap(m_pViewProjCB, 0);
    }

    RenderSkybox(vpSky);

    RenderCube(view, proj, (float)currentTime);

    m_pSwapChain->Present(1, 0);
}

void D3D11Renderer::Resize(UINT newWidth, UINT newHeight)
{
    if (!m_pSwapChain || !m_pDevice || !m_pContext || newWidth == 0 || newHeight == 0)
        return;

    m_pContext->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(m_pBackBufferRTV);
    SAFE_RELEASE(m_pDepthStencilView);

    HRESULT hr = m_pSwapChain->ResizeBuffers(2, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return;

    ID3D11Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer)))
    {
        m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pBackBufferRTV);
        pBackBuffer->Release();
    }

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = newWidth;
    depthDesc.Height = newHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencil = nullptr;
    if (SUCCEEDED(m_pDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil)))
    {
        m_pDevice->CreateDepthStencilView(pDepthStencil, nullptr, &m_pDepthStencilView);
        pDepthStencil->Release();
    }

    m_width = newWidth;
    m_height = newHeight;
}

void D3D11Renderer::HandleKey(UINT key, bool isDown)
{
    switch (key)
    {
    case VK_LEFT: m_keyLeft = isDown; break;
    case VK_RIGHT: m_keyRight = isDown; break;
    case VK_UP: m_keyUp = isDown; break;
    case VK_DOWN: m_keyDown = isDown; break;
    }
}