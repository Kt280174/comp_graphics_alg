#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cassert>
#include <string>
#include <vector>
#include <cstdio> 
#include <algorithm> 

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
     ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace DirectX;

const UINT MAX_OBJECTS = 10;
const UINT NUM_MATERIALS = 2;
const std::wstring MATERIAL_PATHS[] = {
    L"Brick.dds",
    L"Kitty.dds"
};

std::wstring GetAppPath()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        path = path.substr(0, pos + 1);
    return path;
}

struct DDS_PIXELFORMAT
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwABitMask;
};

struct DDS_HEADER
{
    DWORD dwSize;
    DWORD dwHeaderFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    DWORD dwPitchOrLinearSize;
    DWORD dwDepth;
    DWORD dwMipMapCount;
    DWORD dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    DWORD dwSurfaceFlags;
    DWORD dwCubemapFlags;
    DWORD dwReserved2[3];
};

#define DDS_MAGIC 0x20534444
#define DDS_HEADER_FLAGS_TEXTURE 0x00001007
#define DDS_SURFACE_FLAGS_MIPMAP 0x00400000
#define DDS_FOURCC 0x00000004
#define DDS_RGB 0x00000040

#define FOURCC_DXT1 MAKEFOURCC('D','X','T','1')
#define FOURCC_DXT3 MAKEFOURCC('D','X','T','3')
#define FOURCC_DXT5 MAKEFOURCC('D','X','T','5')

inline UINT DivUp(UINT a, UINT b) { return (a + b - 1) / b; }

UINT GetBytesPerBlock(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC4_UNORM:
        return 8;
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC5_UNORM:
        return 16;
    default:
        return 0;
    }
}

struct VertexData
{
    XMFLOAT3 position;
    XMFLOAT2 texCoord;
};

struct DetailedVertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT3 tangent;
    XMFLOAT2 texCoord;
};

struct TextureInfo
{
    UINT32 pitch = 0;
    UINT32 mipLevels = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT32 width = 0;
    UINT32 height = 0;
    void* data = nullptr;
};

bool LoadDDSFile(const wchar_t* filename, TextureInfo& info)
{
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD dwMagic;
    DWORD dwBytesRead;
    ReadFile(hFile, &dwMagic, sizeof(DWORD), &dwBytesRead, NULL);
    if (dwMagic != DDS_MAGIC)
    {
        CloseHandle(hFile);
        return false;
    }

    DDS_HEADER header;
    ReadFile(hFile, &header, sizeof(DDS_HEADER), &dwBytesRead, NULL);

    info.width = header.dwWidth;
    info.height = header.dwHeight;
    info.mipLevels = (header.dwSurfaceFlags & DDS_SURFACE_FLAGS_MIPMAP) ? header.dwMipMapCount : 1;

    if (header.ddspf.dwFlags & DDS_FOURCC)
    {
        switch (header.ddspf.dwFourCC)
        {
        case FOURCC_DXT1:
            info.format = DXGI_FORMAT_BC1_UNORM;
            break;
        case FOURCC_DXT3:
            info.format = DXGI_FORMAT_BC2_UNORM;
            break;
        case FOURCC_DXT5:
            info.format = DXGI_FORMAT_BC3_UNORM;
            break;
        default:
            info.format = DXGI_FORMAT_UNKNOWN;
            break;
        }
    }
    else if (header.ddspf.dwFlags & DDS_RGB)
    {
        info.format = DXGI_FORMAT_UNKNOWN;
    }

    if (info.format == DXGI_FORMAT_UNKNOWN)
    {
        CloseHandle(hFile);
        return false;
    }

    UINT32 blockWidth = DivUp(info.width, 4u);
    UINT32 blockHeight = DivUp(info.height, 4u);
    UINT32 pitch = blockWidth * GetBytesPerBlock(info.format);
    UINT32 dataSize = pitch * blockHeight;

    info.data = malloc(dataSize);
    if (!info.data)
    {
        CloseHandle(hFile);
        return false;
    }
    ReadFile(hFile, info.data, dataSize, &dwBytesRead, NULL);

    CloseHandle(hFile);
    return true;
}

HWND g_hMainWnd = nullptr;

ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* g_pContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pBackBufferRTV = nullptr;
ID3D11DepthStencilView* g_pDepthView = nullptr;
ID3D11RasterizerState* g_pRSCullNone = nullptr;
ID3D11ShaderResourceView* g_pNormalMapSRV = nullptr;

ID3D11Buffer* g_pCubeVB = nullptr;
ID3D11Buffer* g_pCubeIB = nullptr;

ID3D11VertexShader* g_pMainVS = nullptr;
ID3D11PixelShader* g_pMainPS = nullptr;
ID3D11InputLayout* g_pMainLayout = nullptr;

ID3D11VertexShader* g_pSkyboxVS = nullptr;
ID3D11PixelShader* g_pSkyboxPS = nullptr;
ID3D11InputLayout* g_pSkyboxLayout = nullptr;
ID3D11Buffer* g_pSkyboxVB = nullptr;
ID3D11Buffer* g_pSkyboxIB = nullptr;

ID3D11VertexShader* g_pInstancedVS = nullptr;
ID3D11PixelShader* g_pInstancedPS = nullptr;
ID3D11InputLayout* g_pInstancedLayout = nullptr;

struct ModelConstants
{
    XMMATRIX world;
};
struct ViewProjectionConstants
{
    XMMATRIX viewProj;
};

struct SceneConstants
{
    XMMATRIX viewProj;
    XMFLOAT4 cameraPosition;
    XMFLOAT4 lightCount;
    struct LightSource {
        XMFLOAT4 position;
        XMFLOAT4 color;
    } lights[10];
    XMFLOAT4 ambientColor;
};

struct InstanceData
{
    XMMATRIX worldMatrix;
    XMMATRIX normalMatrix;
    XMFLOAT4 materialProps;
    XMFLOAT4 rotationAngle;
};

ID3D11Buffer* g_pSceneCB = nullptr;
ID3D11Buffer* g_pModelCB = nullptr;
ID3D11Buffer* g_pViewProjCB = nullptr;

ID3D11ShaderResourceView* g_pMainTexSRV = nullptr;
ID3D11ShaderResourceView* g_pCubemapSRV = nullptr;
ID3D11SamplerState* g_pDefaultSampler = nullptr;

ID3D11DepthStencilState* g_pDepthNoWrite = nullptr;
ID3D11RasterizerState* g_pRSCullBack = nullptr;

ID3D11Buffer* g_pInstanceBuffer = nullptr;
ID3D11Buffer* g_pVisibleIndicesBuffer = nullptr;

InstanceData g_InstancesData[MAX_OBJECTS];
UINT g_ActiveInstanceCount = 0;
XMVECTOR g_LocalBoundsMin = XMVectorSet(-0.5f, -0.5f, -0.5f, 0.0f);
XMVECTOR g_LocalBoundsMax = XMVectorSet(0.5f, 0.5f, 0.5f, 0.0f);

ID3D11ShaderResourceView* g_pTextureArraySRV = nullptr;

UINT g_ScreenWidth = 1280;
UINT g_ScreenHeight = 720;
float g_CameraYaw = 0.0f;
float g_CameraPitch = 0.0f;
float g_CameraDistance = 15.0f;
bool g_KeyLeft = false, g_KeyRight = false, g_KeyUp = false, g_KeyDown = false;
double g_PreviousFrameTime = 0.0;

#define SAFE_RELEASE(p) if (p) { (p)->Release(); (p) = nullptr; }

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
bool InitializeD3D();
void CreateGeometryBuffers();
void CompileShaders();
void LoadTextureResources();
void CleanupD3D();
void RenderScene();
void ResizeWindow(UINT newWidth, UINT newHeight);
void UpdateCameraMovement(double deltaTime);
void CreateColorBuffer(UINT width, UINT height);

void CreateInstances();
void LoadTextureArrayResource();
void ExtractFrustumPlanes(const XMMATRIX& vp, XMVECTOR planes[6]);
void TransformBounds(const XMMATRIX& transform, const XMVECTOR& localMin, const XMVECTOR& localMax, XMVECTOR& worldMin, XMVECTOR& worldMax);
bool IsBoundsVisible(const XMVECTOR planes[6], const XMVECTOR& boundsMin, const XMVECTOR& boundsMax);
void UpdateInstanceTransforms(double time);

ID3D11Texture2D* g_pColorBufferTex = nullptr;
ID3D11RenderTargetView* g_pColorBufferRTV = nullptr;
ID3D11ShaderResourceView* g_pColorBufferSRV = nullptr;

bool g_ApplyFilter = true;
ID3D11PixelShader* g_pFilterPS = nullptr;
ID3D11VertexShader* g_pFilterVS = nullptr;

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE,
    _In_ LPWSTR, _In_ int nCmdShow)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"D3D11RenderClass";

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    RECT rc = { 0, 0, (LONG)g_ScreenWidth, (LONG)g_ScreenHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    g_hMainWnd = CreateWindowW(wc.lpszClassName, L"Graphics Lab: Instanced Rendering",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        winWidth, winHeight, nullptr, nullptr, hInstance, nullptr);
    if (!g_hMainWnd)
    {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    if (!InitializeD3D())
    {
        CleanupD3D();
        DestroyWindow(g_hMainWnd);
        return -1;
    }

    CreateGeometryBuffers();
    CreateColorBuffer(g_ScreenWidth, g_ScreenHeight);
    CompileShaders();
    LoadTextureResources();
    LoadTextureArrayResource();
    CreateInstances();

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(ModelConstants);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_pDevice->CreateBuffer(&desc, nullptr, &g_pModelCB);

    desc.ByteWidth = sizeof(ViewProjectionConstants);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_pDevice->CreateBuffer(&desc, nullptr, &g_pViewProjCB);

    desc.ByteWidth = sizeof(SceneConstants);
    g_pDevice->CreateBuffer(&desc, nullptr, &g_pSceneCB);

    desc.ByteWidth = sizeof(InstanceData) * MAX_OBJECTS;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = 0;
    g_pDevice->CreateBuffer(&desc, nullptr, &g_pInstanceBuffer);

    desc.ByteWidth = sizeof(XMUINT4) * MAX_OBJECTS;
    g_pDevice->CreateBuffer(&desc, nullptr, &g_pVisibleIndicesBuffer);

    g_PreviousFrameTime = (double)GetTickCount64() / 1000.0;

    MSG msg = {};
    bool done = false;
    while (!done)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                done = true;

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!done)
            RenderScene();
    }

    CleanupD3D();
    return (int)msg.wParam;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        if (g_pSwapChain && wParam != SIZE_MINIMIZED)
        {
            UINT newW = LOWORD(lParam);
            UINT newH = HIWORD(lParam);
            if (newW > 0 && newH > 0)
                ResizeWindow(newW, newH);
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_LEFT)  g_KeyLeft = true;
        if (wParam == VK_RIGHT) g_KeyRight = true;
        if (wParam == VK_UP)    g_KeyUp = true;
        if (wParam == VK_DOWN)  g_KeyDown = true;
        if (wParam == 'F')      g_ApplyFilter = !g_ApplyFilter;
        return 0;

    case WM_KEYUP:
        if (wParam == VK_LEFT)  g_KeyLeft = false;
        if (wParam == VK_RIGHT) g_KeyRight = false;
        if (wParam == VK_UP)    g_KeyUp = false;
        if (wParam == VK_DOWN)  g_KeyDown = false;
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

bool InitializeD3D()
{
    HRESULT hr;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = g_ScreenWidth;
    scd.BufferDesc.Height = g_ScreenHeight;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = g_hMainWnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtainedLevel;

    hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        flags, levels, 1, D3D11_SDK_VERSION,
        &scd, &g_pSwapChain, &g_pDevice, &obtainedLevel, &g_pContext
    );

    if (FAILED(hr))
        return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) return false;

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = g_ScreenWidth;
    depthDesc.Height = g_ScreenHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencil = nullptr;
    hr = g_pDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (FAILED(hr)) return false;

    hr = g_pDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_pDepthView);
    pDepthStencil->Release();
    if (FAILED(hr)) return false;

    return true;
}

void CreateColorBuffer(UINT width, UINT height)
{
    SAFE_RELEASE(g_pColorBufferTex);
    SAFE_RELEASE(g_pColorBufferRTV);
    SAFE_RELEASE(g_pColorBufferSRV);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.ArraySize = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.Height = height;
    desc.Width = width;
    desc.MipLevels = 1;

    HRESULT hr = g_pDevice->CreateTexture2D(&desc, nullptr, &g_pColorBufferTex);
    if (SUCCEEDED(hr))
        hr = g_pDevice->CreateRenderTargetView(g_pColorBufferTex, nullptr, &g_pColorBufferRTV);
    if (SUCCEEDED(hr))
        hr = g_pDevice->CreateShaderResourceView(g_pColorBufferTex, nullptr, &g_pColorBufferSRV);
    assert(SUCCEEDED(hr));
}

void CreateGeometryBuffers()
{
    const DetailedVertex cubeVertices[] = {
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,1) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,1) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,0) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,0) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,0) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,0) }
    };

    const USHORT cubeIndices[] = {
         0,  2,  1,  0,  3,  2,
         4,  5,  6,  4,  6,  7,
         8, 10,  9,  8, 11, 10,
        12, 14, 13, 12, 15, 14,
        16, 18, 17, 16, 19, 18,
        20, 22, 21, 20, 23, 22
    };

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(cubeVertices);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = { cubeVertices };
    g_pDevice->CreateBuffer(&desc, &data, &g_pCubeVB);

    desc.ByteWidth = sizeof(cubeIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = cubeIndices;
    g_pDevice->CreateBuffer(&desc, &data, &g_pCubeIB);

    const VertexData skyboxVertices[] = {
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
        0,2,1, 0,3,2,  4,5,6, 4,6,7,  0,7,3, 0,4,7,
        1,2,6, 1,6,5,  3,7,6, 3,6,2,  0,1,5, 0,5,4
    };

    desc.ByteWidth = sizeof(skyboxVertices);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem = skyboxVertices;
    g_pDevice->CreateBuffer(&desc, &data, &g_pSkyboxVB);

    desc.ByteWidth = sizeof(skyboxIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = skyboxIndices;
    g_pDevice->CreateBuffer(&desc, &data, &g_pSkyboxIB);
}

void CompileShaders()
{
    const char* vertexShaderCode = R"(
        cbuffer ModelCB : register(b0) { float4x4 world; }
        cbuffer ViewProjCB : register(b1) { float4x4 viewProj; }
        struct VSInput {
            float3 position : POSITION;
            float3 normal : NORMAL;
            float3 tangent : TANGENT;
            float2 texCoord : TEXCOORD;
        };
        struct VSOutput {
            float4 clipPos : SV_Position;
            float3 worldPos : TEXCOORD0;
            float3 worldNormal : NORMAL;
            float3 worldTangent : TANGENT;
            float2 texCoord : TEXCOORD1;
        };
        VSOutput MainVS(VSInput input) {
            VSOutput output;
            float4 worldPos = mul(float4(input.position, 1.0), world);
            output.clipPos = mul(worldPos, viewProj);
            output.worldPos = worldPos.xyz;
            float3x3 normalMatrix = (float3x3)world;
            output.worldNormal = normalize(mul(input.normal, normalMatrix));
            output.worldTangent = normalize(mul(input.tangent, normalMatrix));
            output.texCoord = input.texCoord;
            return output;
        }
    )";

    const char* pixelShaderCode = R"(
        Texture2D colorTexture : register(t0);
        Texture2D normalMap : register(t1);
        SamplerState textureSampler : register(s0);
        cbuffer SceneCB : register(b2) {
            float4x4 viewProj;
            float4 cameraPos;
            float4 lightCount;
            struct LightData {
                float4 position;
                float4 color;
            } lights[10];
            float4 ambientColor;
        };
        struct VSOutput {
            float4 clipPos : SV_Position;
            float3 worldPos : TEXCOORD0;
            float3 worldNormal : NORMAL;
            float3 worldTangent : TANGENT;
            float2 texCoord : TEXCOORD1;
        };
        float4 MainPS(VSOutput input) : SV_Target0 {
            float4 texColor = colorTexture.Sample(textureSampler, input.texCoord);
            float3 tangentNormal = normalMap.Sample(textureSampler, input.texCoord).xyz * 2.0 - 1.0;
            float3 N = normalize(input.worldNormal);
            float3 T = normalize(input.worldTangent);
            float3 B = cross(N, T);
            float3x3 TBN = float3x3(T, B, N);
            float3 finalNormal = normalize(mul(tangentNormal, TBN));
            float3 finalColor = ambientColor.xyz * texColor.xyz;
            for (int i = 0; i < lightCount.x; ++i) {
                float3 L = lights[i].position.xyz - input.worldPos.xyz;
                float dist = length(L);
                L = L / dist;
                float attenuation = 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);
                float diffuse = max(dot(finalNormal, L), 0.0);
                finalColor += texColor.xyz * diffuse * attenuation * lights[i].color.xyz;
                float3 V = normalize(cameraPos.xyz - input.worldPos.xyz);
                float3 R = reflect(-L, finalNormal);
                float specular = pow(max(dot(V, R), 0.0), 32.0);
                finalColor += specular * attenuation * lights[i].color.xyz;
            }
            return float4(finalColor, 1.0);
        }
    )";

    const char* skyboxVS = R"(
        cbuffer ViewProjCB : register(b1) { float4x4 viewProj; }
        struct VSInput { float3 position : POSITION; float2 texCoord : TEXCOORD; };
        struct VSOutput { float4 clipPos : SV_Position; float3 localPos : TEXCOORD; };
        VSOutput MainVS(VSInput input) {
            VSOutput output;
            output.clipPos = mul(float4(input.position, 1.0), viewProj);
            output.localPos = input.position;
            return output;
        }
    )";

    const char* skyboxPS = R"(
        TextureCube skyboxTexture : register(t1);
        SamplerState skyboxSampler : register(s1);
        struct VSOutput { float4 clipPos : SV_Position; float3 localPos : TEXCOORD; };
        float4 MainPS(VSOutput input) : SV_Target0 {
            return skyboxTexture.Sample(skyboxSampler, input.localPos);
        }
    )";

    const char* instancedVS = R"(
        cbuffer InstanceBuffer : register(b1)
        {
            struct InstanceData
            {
                float4x4 worldMatrix;
                float4x4 normalMatrix;
                float4 materialProps;
                float4 rotationAngle;
            } instances[10];
        };
        cbuffer ViewProjCB : register(b2)
        {
            float4x4 viewProj;
        };
        struct VSInput
        {
            float3 position    : POSITION;
            float3 tangent     : TANGENT;
            float3 normal      : NORMAL;
            float2 texCoord    : TEXCOORD;
            uint instanceId    : SV_InstanceID;
        };
        struct VSOutput
        {
            float4 clipPos       : SV_Position;
            float4 worldPos      : POSITION;
            float3 tangent       : TANGENT;
            float3 normal        : NORMAL;
            float2 texCoord      : TEXCOORD;
            nointerpolation uint instanceId : INST_ID;
        };
        VSOutput MainVS(VSInput input)
        {
            VSOutput output;
            uint idx = input.instanceId;
            float4 worldPos = mul(instances[idx].worldMatrix, float4(input.position, 1.0));
            output.clipPos = mul(worldPos, viewProj);
            output.worldPos = worldPos;
            output.texCoord = input.texCoord;
            output.tangent = mul(instances[idx].normalMatrix, float4(input.tangent, 0)).xyz;
            output.normal = mul(instances[idx].normalMatrix, float4(input.normal, 0)).xyz;
            output.instanceId = idx;
            return output;
        }
    )";

    const char* instancedPS = R"(
        Texture2DArray colorTexture : register(t0);
        Texture2D normalMapTexture : register(t1);
        SamplerState textureSampler : register(s0);
        cbuffer InstanceBuffer : register(b1)
        {
            struct InstanceData
            {
                float4x4 worldMatrix;
                float4x4 normalMatrix;
                float4 materialProps;
                float4 rotationAngle;
            } instances[10];
        };
        cbuffer SceneCB : register(b3)
        {
            float4x4 viewProj;
            float4 cameraPos;
            float4 lightCount;
            struct LightData
            {
                float4 position;
                float4 color;
            } lights[10];
            float4 ambientColor;
        };
        cbuffer VisibleIndices : register(b4)
        {
            uint4 visibleIds[10];
        };
        struct VSOutput
        {
            float4 clipPos       : SV_Position;
            float4 worldPos      : POSITION;
            float3 tangent       : TANGENT;
            float3 normal        : NORMAL;
            float2 texCoord      : TEXCOORD;
            nointerpolation uint instanceId : INST_ID;
        };
        float4 MainPS(VSOutput input) : SV_Target0
        {
            uint idx = visibleIds[input.instanceId].x;
            uint texId = (uint)instances[idx].materialProps.z;
            float3 color = colorTexture.Sample(textureSampler, float3(input.texCoord, texId)).xyz;
            uint flags = asuint(instances[idx].materialProps.w);
            float3 finalNormal;
            if (flags == 1 && lightCount.y > 0)
            {
                float3 tangentNormal = normalMapTexture.Sample(textureSampler, input.texCoord).xyz * 2.0 - 1.0;
                float3 N = normalize(input.normal);
                float3 T = normalize(input.tangent);
                float3 B = cross(N, T);
                finalNormal = normalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * N);
            }
            else
            {
                finalNormal = normalize(input.normal);
            }
            float shininess = instances[idx].materialProps.x;
            float3 finalColor = ambientColor.xyz * color;
            for (int i = 0; i < lightCount.x; ++i)
            {
                float3 L = lights[i].position.xyz - input.worldPos.xyz;
                float dist = length(L);
                L = L / dist;
                float attenuation = 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);
                float diffuse = max(dot(finalNormal, L), 0.0);
                finalColor += color * diffuse * attenuation * lights[i].color.xyz;
                float3 V = normalize(cameraPos.xyz - input.worldPos.xyz);
                float3 R = reflect(-L, finalNormal);
                float specular = pow(max(dot(V, R), 0.0), shininess);
                finalColor += specular * attenuation * lights[i].color.xyz;
            }
            return float4(finalColor, 1.0);
        }
    )";

    const char* filterVS = R"(
        struct VSInput { uint vertexId : SV_VertexID; };
        struct VSOutput { float4 clipPos : SV_Position; float2 texCoord : TEXCOORD; };
        VSOutput MainVS(VSInput input) {
            VSOutput output;
            float4 pos = float4(0,0,0,0);
            switch (input.vertexId) {
                case 0: pos = float4(-1, 1, 0, 1); break;
                case 1: pos = float4(3,  1, 0, 1); break;
                case 2: pos = float4( -1, -3, 0, 1); break;
            }
            output.clipPos = pos;
            output.texCoord = float2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);
            return output;
        }
    )";

    const char* filterPS = R"(
        Texture2D colorTexture : register(t0);
        SamplerState textureSampler : register(s0);
        struct VSOutput { float4 clipPos : SV_Position; float2 texCoord : TEXCOORD; };
        float4 MainPS(VSOutput input) : SV_Target0 {
            float3 color = colorTexture.Sample(textureSampler, input.texCoord).rgb;
            float grayscale = dot(color, float3(0.299, 0.587, 0.114));
            return float4(grayscale, grayscale, grayscale, 1.0);
        }
    )";

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pVsBlob = nullptr, * pPsBlob = nullptr, * pErrorBlob = nullptr;

    D3DCompile(vertexShaderCode, strlen(vertexShaderCode), nullptr, nullptr, nullptr, "MainVS", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pMainVS);

    D3DCompile(pixelShaderCode, strlen(pixelShaderCode), nullptr, nullptr, nullptr, "MainPS", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pMainPS);
    SAFE_RELEASE(pPsBlob);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_pDevice->CreateInputLayout(layout, 4, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pMainLayout);
    SAFE_RELEASE(pVsBlob);

    D3DCompile(instancedVS, strlen(instancedVS), nullptr, nullptr, nullptr, "MainVS", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pInstancedVS);

    D3DCompile(instancedPS, strlen(instancedPS), nullptr, nullptr, nullptr, "MainPS", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pInstancedPS);
    SAFE_RELEASE(pPsBlob);

    D3D11_INPUT_ELEMENT_DESC layoutInst[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_pDevice->CreateInputLayout(layoutInst, 4, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pInstancedLayout);
    SAFE_RELEASE(pVsBlob);

    D3DCompile(skyboxVS, strlen(skyboxVS), nullptr, nullptr, nullptr, "MainVS", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pSkyboxVS);

    D3DCompile(skyboxPS, strlen(skyboxPS), nullptr, nullptr, nullptr, "MainPS", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob);
    if (pErrorBlob) { OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer()); pErrorBlob->Release(); }
    g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pSkyboxPS);

    D3D11_INPUT_ELEMENT_DESC layoutSky[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_pDevice->CreateInputLayout(layoutSky, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pSkyboxLayout);

    ID3DBlob* pFilterVSBlob = nullptr;
    ID3DBlob* pFilterPSBlob = nullptr;
    D3DCompile(filterVS, strlen(filterVS), nullptr, nullptr, nullptr, "MainVS", "vs_5_0", flags, 0, &pFilterVSBlob, &pErrorBlob);
    if (pErrorBlob)
    {
        if (pErrorBlob) OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
        SAFE_RELEASE(pErrorBlob);
    }
    else
    {
        g_pDevice->CreateVertexShader(pFilterVSBlob->GetBufferPointer(), pFilterVSBlob->GetBufferSize(), nullptr, &g_pFilterVS);
        SAFE_RELEASE(pFilterVSBlob);
    }

    D3DCompile(filterPS, strlen(filterPS), nullptr, nullptr, nullptr, "MainPS", "ps_5_0", flags, 0, &pFilterPSBlob, &pErrorBlob);
    if (pErrorBlob) {
        OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
        SAFE_RELEASE(pErrorBlob);
    }
    else
    {
        g_pDevice->CreatePixelShader(pFilterPSBlob->GetBufferPointer(), pFilterPSBlob->GetBufferSize(), nullptr, &g_pFilterPS);
        SAFE_RELEASE(pFilterPSBlob);
    }

    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);
}

void LoadTextureResources()
{
    HRESULT hr;

    std::wstring path = GetAppPath() + L"..\\..\\texture\\skybox\\";
    std::wstring faceNames[6] = {
        path + L"posx.dds", path + L"negx.dds",
        path + L"posy.dds", path + L"negy.dds",
        path + L"posz.dds", path + L"negz.dds"
    };

    TextureInfo faceInfos[6];
    bool allOk = true;
    for (int i = 0; i < 6; ++i)
    {
        if (!LoadDDSFile(faceNames[i].c_str(), faceInfos[i]))
        {
            allOk = false;
            break;
        }
    }

    if (allOk)
    {
        for (int i = 1; i < 6; ++i)
        {
            if (faceInfos[i].format != faceInfos[0].format ||
                faceInfos[i].width != faceInfos[0].width ||
                faceInfos[i].height != faceInfos[0].height)
            {
                MessageBoxA(NULL, "Cubemap faces must be identical", "Error", MB_OK);
                allOk = false;
                break;
            }
        }
    }

    if (allOk)
    {
        D3D11_TEXTURE2D_DESC cubeDesc = {};
        cubeDesc.Width = faceInfos[0].width;
        cubeDesc.Height = faceInfos[0].height;
        cubeDesc.MipLevels = 1;
        cubeDesc.ArraySize = 6;
        cubeDesc.Format = faceInfos[0].format;
        cubeDesc.SampleDesc.Count = 1;
        cubeDesc.SampleDesc.Quality = 0;
        cubeDesc.Usage = D3D11_USAGE_IMMUTABLE;
        cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

        UINT blockWidth = DivUp(cubeDesc.Width, 4u);
        UINT blockHeight = DivUp(cubeDesc.Height, 4u);
        UINT pitch = blockWidth * GetBytesPerBlock(cubeDesc.Format);

        D3D11_SUBRESOURCE_DATA initData[6];
        for (int i = 0; i < 6; ++i)
        {
            initData[i].pSysMem = faceInfos[i].data;
            initData[i].SysMemPitch = pitch;
            initData[i].SysMemSlicePitch = 0;
        }

        ID3D11Texture2D* pCubemapTex = nullptr;
        hr = g_pDevice->CreateTexture2D(&cubeDesc, initData, &pCubemapTex);
        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC cubeSRVDesc = {};
            cubeSRVDesc.Format = cubeDesc.Format;
            cubeSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            cubeSRVDesc.TextureCube.MipLevels = 1;
            cubeSRVDesc.TextureCube.MostDetailedMip = 0;
            hr = g_pDevice->CreateShaderResourceView(pCubemapTex, &cubeSRVDesc, &g_pCubemapSRV);
            pCubemapTex->Release();
        }
    }

    for (int i = 0; i < 6; ++i)
        if (faceInfos[i].data) free(faceInfos[i].data);

    TextureInfo texInfo;
    std::wstring normalPath = GetAppPath() + L"..\\..\\texture\\BrickNM.dds";
    if (LoadDDSFile(normalPath.c_str(), texInfo))
    {
        D3D11_TEXTURE2D_DESC normTexDesc = {};
        normTexDesc.Width = texInfo.width;
        normTexDesc.Height = texInfo.height;
        normTexDesc.MipLevels = 1;
        normTexDesc.ArraySize = 1;
        normTexDesc.Format = texInfo.format;
        normTexDesc.SampleDesc.Count = 1;
        normTexDesc.SampleDesc.Quality = 0;
        normTexDesc.Usage = D3D11_USAGE_IMMUTABLE;
        normTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        UINT normBlockWidth = DivUp(texInfo.width, 4u);
        UINT normBlockHeight = DivUp(texInfo.height, 4u);
        UINT normPitch = normBlockWidth * GetBytesPerBlock(texInfo.format);

        D3D11_SUBRESOURCE_DATA normData = {};
        normData.pSysMem = texInfo.data;
        normData.SysMemPitch = normPitch;

        ID3D11Texture2D* pNormalTex = nullptr;
        HRESULT hr = g_pDevice->CreateTexture2D(&normTexDesc, &normData, &pNormalTex);
        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = texInfo.format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            hr = g_pDevice->CreateShaderResourceView(pNormalTex, &srvDesc, &g_pNormalMapSRV);
            pNormalTex->Release();
        }
        free(texInfo.data);
    }
    else {
        OutputDebugStringA("No normal map loaded, using flat normals\n");
    }

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MinLOD = -FLT_MAX;
    sampDesc.MaxLOD = FLT_MAX;
    sampDesc.MipLODBias = 0.0f;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] = sampDesc.BorderColor[3] = 1.0f;
    g_pDevice->CreateSamplerState(&sampDesc, &g_pDefaultSampler);

    D3D11_DEPTH_STENCIL_DESC dsNoWriteDesc = {};
    dsNoWriteDesc.DepthEnable = TRUE;
    dsNoWriteDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsNoWriteDesc.DepthFunc = D3D11_COMPARISON_LESS;
    dsNoWriteDesc.StencilEnable = FALSE;
    g_pDevice->CreateDepthStencilState(&dsNoWriteDesc, &g_pDepthNoWrite);

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.FrontCounterClockwise = FALSE;
    g_pDevice->CreateRasterizerState(&rsDesc, &g_pRSCullBack);

    D3D11_RASTERIZER_DESC rsCullNoneDesc = {};
    rsCullNoneDesc.FillMode = D3D11_FILL_SOLID;
    rsCullNoneDesc.CullMode = D3D11_CULL_NONE;
    rsCullNoneDesc.FrontCounterClockwise = FALSE;
    g_pDevice->CreateRasterizerState(&rsCullNoneDesc, &g_pRSCullNone);
}

void LoadTextureArrayResource()
{
    std::vector<TextureInfo> texInfos(NUM_MATERIALS);
    bool allOk = true;
    for (UINT i = 0; i < NUM_MATERIALS; ++i)
    {
        std::wstring fullPath = GetAppPath() + L"..\\..\\texture\\" + MATERIAL_PATHS[i];
        if (!LoadDDSFile(fullPath.c_str(), texInfos[i]))
        {
            allOk = false;
            break;
        }
    }
    if (!allOk)
    {
        MessageBoxA(NULL, "Failed to load one of the textures for array", "Error", MB_OK);
        return;
    }

    DXGI_FORMAT fmt = texInfos[0].format;
    UINT width = texInfos[0].width;
    UINT height = texInfos[0].height;
    for (UINT i = 1; i < NUM_MATERIALS; ++i)
    {
        if (texInfos[i].format != fmt || texInfos[i].width != width || texInfos[i].height != height)
        {
            MessageBoxA(NULL, "Textures must have same format and size", "Error", MB_OK);
            return;
        }
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = NUM_MATERIALS;
    texDesc.Format = fmt;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    UINT blockWidth = DivUp(width, 4u);
    UINT blockHeight = DivUp(height, 4u);
    UINT pitch = blockWidth * GetBytesPerBlock(fmt);

    std::vector<D3D11_SUBRESOURCE_DATA> initData(NUM_MATERIALS);
    for (UINT i = 0; i < NUM_MATERIALS; ++i)
    {
        initData[i].pSysMem = texInfos[i].data;
        initData[i].SysMemPitch = pitch;
        initData[i].SysMemSlicePitch = 0;
    }

    ID3D11Texture2D* pTexArray = nullptr;
    HRESULT hr = g_pDevice->CreateTexture2D(&texDesc, initData.data(), &pTexArray);
    for (auto& td : texInfos) free(td.data);
    if (FAILED(hr)) return;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.ArraySize = NUM_MATERIALS;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    hr = g_pDevice->CreateShaderResourceView(pTexArray, &srvDesc, &g_pTextureArraySRV);
    pTexArray->Release();
}

void CreateInstances()
{
    g_ActiveInstanceCount = MAX_OBJECTS;

    // Định nghĩa kích thước khác nhau cho từng object
    float sizes[] = { 0.5f, 0.6f, 0.4f, 0.7f, 0.5f, 0.8f, 0.4f, 0.6f, 0.5f, 0.7f };

    // Vị trí khác nhau thay vì xếp trên mặt cầu
    XMFLOAT3 positions[] = {
        XMFLOAT3(-2.0f, 0.0f, -2.0f),
        XMFLOAT3(2.0f, 0.0f, -2.0f),
        XMFLOAT3(0.0f, 1.0f, -2.0f),
        XMFLOAT3(-2.0f, -1.0f, 0.0f),
        XMFLOAT3(2.0f, -1.0f, 0.0f),
        XMFLOAT3(0.0f, 2.0f, 0.0f),
        XMFLOAT3(-1.5f, 0.5f, 2.0f),
        XMFLOAT3(1.5f, 0.5f, 2.0f),
        XMFLOAT3(0.0f, -0.5f, 3.0f),
        XMFLOAT3(0.0f, 1.5f, -3.0f)
    };

    for (UINT i = 0; i < MAX_OBJECTS; ++i)
    {
        // Tạo ma trận scale với kích thước khác nhau
        float size = sizes[i % 10];
        XMMATRIX scale = XMMatrixScaling(size, size, size);

        // Tạo ma trận translation với vị trí khác nhau
        XMMATRIX translation = XMMatrixTranslation(positions[i].x, positions[i].y, positions[i].z);

        // Kết hợp scale và translation
        XMMATRIX world = scale * translation;
        XMMATRIX normal = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

        g_InstancesData[i].worldMatrix = world;
        g_InstancesData[i].normalMatrix = normal;

        // Random texture ID
        int texId = rand() % NUM_MATERIALS;
        float shininess = 16.0f + (rand() % 48);
        float rotSpeed = 0.2f + (rand() % 100) / 100.0f;
        float normalMapPresence = (texId == 1) ? 1.0f : 0.0f;

        g_InstancesData[i].materialProps = XMFLOAT4(shininess, rotSpeed, (float)texId, normalMapPresence);
        g_InstancesData[i].rotationAngle = XMFLOAT4(positions[i].x, positions[i].y, positions[i].z, 0.0f);
    }
}

void UpdateInstanceTransforms(double time)
{
    for (UINT i = 0; i < g_ActiveInstanceCount; ++i)
    {
        float angle = (float)time * g_InstancesData[i].materialProps.y;
        XMMATRIX rot = XMMatrixRotationY(angle);
        XMMATRIX trans = XMMatrixTranslation(g_InstancesData[i].rotationAngle.x,
            g_InstancesData[i].rotationAngle.y,
            g_InstancesData[i].rotationAngle.z);
        g_InstancesData[i].worldMatrix = rot * trans;
        g_InstancesData[i].normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, g_InstancesData[i].worldMatrix));
    }
}

void ExtractFrustumPlanes(const XMMATRIX& vp, XMVECTOR planes[6])
{
    XMVECTOR row1 = XMVectorSet(vp.r[0].m128_f32[0], vp.r[1].m128_f32[0], vp.r[2].m128_f32[0], vp.r[3].m128_f32[0]);
    XMVECTOR row2 = XMVectorSet(vp.r[0].m128_f32[1], vp.r[1].m128_f32[1], vp.r[2].m128_f32[1], vp.r[3].m128_f32[1]);
    XMVECTOR row3 = XMVectorSet(vp.r[0].m128_f32[2], vp.r[1].m128_f32[2], vp.r[2].m128_f32[2], vp.r[3].m128_f32[2]);
    XMVECTOR row4 = XMVectorSet(vp.r[0].m128_f32[3], vp.r[1].m128_f32[3], vp.r[2].m128_f32[3], vp.r[3].m128_f32[3]);

    planes[0] = row4 + row1;
    planes[1] = row4 - row1;
    planes[2] = row4 + row2;
    planes[3] = row4 - row2;
    planes[4] = row4 + row3;
    planes[5] = row4 - row3;

    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR norm = XMVector3Length(planes[i]);
        planes[i] = planes[i] / norm;
    }
}

void TransformBounds(const XMMATRIX& transform, const XMVECTOR& localMin, const XMVECTOR& localMax, XMVECTOR& worldMin, XMVECTOR& worldMax)
{
    XMVECTOR corners[8];
    corners[0] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMin), XMVectorGetZ(localMin), 1.0f);
    corners[1] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMin), XMVectorGetZ(localMin), 1.0f);
    corners[2] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMax), XMVectorGetZ(localMin), 1.0f);
    corners[3] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMax), XMVectorGetZ(localMin), 1.0f);
    corners[4] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMin), XMVectorGetZ(localMax), 1.0f);
    corners[5] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMin), XMVectorGetZ(localMax), 1.0f);
    corners[6] = XMVectorSet(XMVectorGetX(localMin), XMVectorGetY(localMax), XMVectorGetZ(localMax), 1.0f);
    corners[7] = XMVectorSet(XMVectorGetX(localMax), XMVectorGetY(localMax), XMVectorGetZ(localMax), 1.0f);

    worldMin = XMVectorReplicate(FLT_MAX);
    worldMax = XMVectorReplicate(-FLT_MAX);
    for (int i = 0; i < 8; ++i)
    {
        XMVECTOR worldCorner = XMVector4Transform(corners[i], transform);
        worldMin = XMVectorMin(worldMin, worldCorner);
        worldMax = XMVectorMax(worldMax, worldCorner);
    }
}

bool IsBoundsVisible(const XMVECTOR planes[6], const XMVECTOR& boundsMin, const XMVECTOR& boundsMax)
{
    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR p = boundsMin;
        if (XMVectorGetX(planes[i]) >= 0) p = XMVectorSetX(p, XMVectorGetX(boundsMax));
        if (XMVectorGetY(planes[i]) >= 0) p = XMVectorSetY(p, XMVectorGetY(boundsMax));
        if (XMVectorGetZ(planes[i]) >= 0) p = XMVectorSetZ(p, XMVectorGetZ(boundsMax));
        if (XMVector4Dot(p, planes[i]).m128_f32[0] < 0) return false;
    }
    return true;
}

void UpdateCameraMovement(double deltaTime)
{
    float speed = 1.0f;
    if (g_KeyLeft)  g_CameraYaw -= speed * (float)deltaTime;
    if (g_KeyRight) g_CameraYaw += speed * (float)deltaTime;
    if (g_KeyUp)    g_CameraPitch += speed * (float)deltaTime;
    if (g_KeyDown)  g_CameraPitch -= speed * (float)deltaTime;

    const float maxPitch = 1.5f;
    if (g_CameraPitch > maxPitch) g_CameraPitch = maxPitch;
    if (g_CameraPitch < -maxPitch) g_CameraPitch = -maxPitch;
}

void RenderScene()
{
    if (!g_pContext || !g_pBackBufferRTV || !g_pSwapChain)
        return;

    double currentTime = (double)GetTickCount64() / 1000.0;
    double deltaTime = currentTime - g_PreviousFrameTime;
    g_PreviousFrameTime = currentTime;

    UpdateCameraMovement(deltaTime);


    g_pContext->ClearState();

    ID3D11RenderTargetView* sceneTarget = g_ApplyFilter ? g_pColorBufferRTV : g_pBackBufferRTV;
    g_pContext->OMSetRenderTargets(1, &sceneTarget, g_pDepthView);


    const float clearColor[4] = { 0.1f, 0.12f, 0.15f, 1.0f };
    g_pContext->ClearRenderTargetView(sceneTarget, clearColor);
    g_pContext->ClearDepthStencilView(g_pDepthView, D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT vp = { 0, 0, (float)g_ScreenWidth, (float)g_ScreenHeight, 0.0f, 1.0f };
    g_pContext->RSSetViewports(1, &vp);
    g_pContext->RSSetState(g_pRSCullBack);

    // Calculate camera matrices
    float camX = g_CameraDistance * sin(g_CameraYaw) * cos(g_CameraPitch);
    float camY = g_CameraDistance * sin(g_CameraPitch);
    float camZ = g_CameraDistance * cos(g_CameraYaw) * cos(g_CameraPitch);
    XMVECTOR eye = XMVectorSet(camX, camY, camZ, 0.0f);
    XMVECTOR at = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    float aspect = (float)g_ScreenWidth / (float)g_ScreenHeight;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 6.0f, aspect, 0.1f, 25.0f);
    XMMATRIX viewProj = view * proj;


    {
        XMMATRIX viewNoTrans = view;
        viewNoTrans.r[3] = XMVectorSet(0, 0, 0, 1);
        XMMATRIX vpSky = viewNoTrans * proj;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(g_pContext->Map(g_pViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            ViewProjectionConstants* pData = (ViewProjectionConstants*)mapped.pData;
            XMStoreFloat4x4((XMFLOAT4X4*)&pData->viewProj, XMMatrixTranspose(vpSky));
            g_pContext->Unmap(g_pViewProjCB, 0);
        }


        D3D11_DEPTH_STENCIL_DESC dsSky = {};
        dsSky.DepthEnable = TRUE;
        dsSky.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsSky.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        dsSky.StencilEnable = FALSE;
        ID3D11DepthStencilState* pDSSky = nullptr;
        g_pDevice->CreateDepthStencilState(&dsSky, &pDSSky);
        g_pContext->OMSetDepthStencilState(pDSSky, 0);
        SAFE_RELEASE(pDSSky);


        D3D11_RASTERIZER_DESC rsSky = {};
        rsSky.FillMode = D3D11_FILL_SOLID;
        rsSky.CullMode = D3D11_CULL_NONE;
        ID3D11RasterizerState* pRSSky = nullptr;
        g_pDevice->CreateRasterizerState(&rsSky, &pRSSky);
        g_pContext->RSSetState(pRSSky);
        SAFE_RELEASE(pRSSky);

        g_pContext->VSSetShader(g_pSkyboxVS, nullptr, 0);
        g_pContext->PSSetShader(g_pSkyboxPS, nullptr, 0);
        g_pContext->IASetInputLayout(g_pSkyboxLayout);

        UINT stride = sizeof(VertexData);
        UINT offset = 0;
        ID3D11Buffer* vbSky[] = { g_pSkyboxVB };
        g_pContext->IASetVertexBuffers(0, 1, vbSky, &stride, &offset);
        g_pContext->IASetIndexBuffer(g_pSkyboxIB, DXGI_FORMAT_R16_UINT, 0);
        g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11Buffer* cbsSky[] = { nullptr, g_pViewProjCB };
        g_pContext->VSSetConstantBuffers(0, 2, cbsSky);

        ID3D11ShaderResourceView* skySRV[] = { g_pCubemapSRV };
        g_pContext->PSSetShaderResources(1, 1, skySRV);

        ID3D11SamplerState* samplers[] = { g_pDefaultSampler };
        g_pContext->PSSetSamplers(1, 1, samplers);


        g_pContext->DrawIndexed(36, 0, 0);

        g_pContext->RSSetState(g_pRSCullBack);
        g_pContext->OMSetDepthStencilState(nullptr, 0);
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_pContext->Map(g_pViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        ViewProjectionConstants* pData = (ViewProjectionConstants*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pData->viewProj, XMMatrixTranspose(viewProj));
        g_pContext->Unmap(g_pViewProjCB, 0);
    }


    if (SUCCEEDED(g_pContext->Map(g_pSceneCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        SceneConstants* pScene = (SceneConstants*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pScene->viewProj, XMMatrixTranspose(viewProj));
        pScene->cameraPosition = XMFLOAT4(camX, camY, camZ, 1.0f);

        // Use 4 dynamic point lights
        const int NUM_LIGHTS = 4;
        pScene->lightCount.x = (float)NUM_LIGHTS;
        pScene->lightCount.y = 1.0f;
        pScene->lightCount.z = 0.0f;
        pScene->lightCount.w = 0.0f;

        float time = (float)currentTime;


        pScene->lights[0].position = XMFLOAT4(sin(time) * 3.5f, 1.8f, cos(time * 0.8f) * 3.0f, 1.0f);
        pScene->lights[0].color = XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f);


        pScene->lights[1].position = XMFLOAT4(-3.5f, 2.0f, 0.5f, 1.0f);
        pScene->lights[1].color = XMFLOAT4(0.2f, 0.4f, 1.0f, 1.0f);


        pScene->lights[2].position = XMFLOAT4(3.5f, 1.5f, 1.0f, 1.0f);
        pScene->lights[2].color = XMFLOAT4(0.2f, 1.0f, 0.3f, 1.0f);

        pScene->lights[3].position = XMFLOAT4(cos(time * 0.6f) * 3.0f, 1.2f, sin(time * 0.6f) * 3.0f, 1.0f);
        pScene->lights[3].color = XMFLOAT4(1.0f, 0.7f, 0.2f, 1.0f);

        pScene->ambientColor = XMFLOAT4(0.25f, 0.28f, 0.35f, 1.0f);

        g_pContext->Unmap(g_pSceneCB, 0);
    }


    UpdateInstanceTransforms(currentTime);
    g_pContext->UpdateSubresource(g_pInstanceBuffer, 0, nullptr, g_InstancesData, sizeof(InstanceData) * MAX_OBJECTS, 0);

    XMVECTOR frustumPlanes[6];
    ExtractFrustumPlanes(viewProj, frustumPlanes);

    std::vector<UINT> visibleIndices;
    XMVECTOR localMin = XMVectorSet(-0.5f, -0.5f, -0.5f, 1.0f);
    XMVECTOR localMax = XMVectorSet(0.5f, 0.5f, 0.5f, 1.0f);

    for (UINT i = 0; i < g_ActiveInstanceCount; ++i)
    {
        // Extract scale from world matrix for accurate bounds
        XMVECTOR scale, rotQuat, trans;
        XMMatrixDecompose(&scale, &rotQuat, &trans, g_InstancesData[i].worldMatrix);

        // Calculate scaled bounds
        XMVECTOR scaledMin = localMin * scale;
        XMVECTOR scaledMax = localMax * scale;

        XMVECTOR worldMin, worldMax;
        TransformBounds(g_InstancesData[i].worldMatrix, scaledMin, scaledMax, worldMin, worldMax);

        if (IsBoundsVisible(frustumPlanes, worldMin, worldMax))
            visibleIndices.push_back(i);
    }


    std::vector<XMUINT4> packedIds(MAX_OBJECTS);
    for (size_t j = 0; j < visibleIndices.size(); ++j)
        packedIds[j].x = visibleIndices[j];
    g_pContext->UpdateSubresource(g_pVisibleIndicesBuffer, 0, nullptr, packedIds.data(), sizeof(XMUINT4) * MAX_OBJECTS, 0);


    if (visibleIndices.size() > 0)
    {

        UINT stride = sizeof(DetailedVertex);
        UINT offset = 0;
        ID3D11Buffer* vbCube[] = { g_pCubeVB };
        g_pContext->IASetVertexBuffers(0, 1, vbCube, &stride, &offset);
        g_pContext->IASetIndexBuffer(g_pCubeIB, DXGI_FORMAT_R16_UINT, 0);
        g_pContext->IASetInputLayout(g_pInstancedLayout);
        g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


        g_pContext->VSSetShader(g_pInstancedVS, nullptr, 0);
        g_pContext->PSSetShader(g_pInstancedPS, nullptr, 0);


        ID3D11Buffer* cbInstVS[] = { nullptr, g_pInstanceBuffer, g_pViewProjCB };
        g_pContext->VSSetConstantBuffers(0, 3, cbInstVS);

        g_pContext->PSSetConstantBuffers(1, 1, &g_pInstanceBuffer);
        g_pContext->PSSetConstantBuffers(3, 1, &g_pSceneCB);
        g_pContext->PSSetConstantBuffers(4, 1, &g_pVisibleIndicesBuffer);


        ID3D11ShaderResourceView* texArraySRV[] = { g_pTextureArraySRV, g_pNormalMapSRV };
        g_pContext->PSSetShaderResources(0, 2, texArraySRV);

        ID3D11SamplerState* samp = g_pDefaultSampler;
        g_pContext->PSSetSamplers(0, 1, &samp);

        g_pContext->DrawIndexedInstanced(36, (UINT)visibleIndices.size(), 0, 0, 0);
    }

    if (g_ApplyFilter)
    {
        // Switch to back buffer
        g_pContext->OMSetRenderTargets(1, &g_pBackBufferRTV, nullptr);
        g_pContext->ClearRenderTargetView(g_pBackBufferRTV, clearColor);

        g_pContext->OMSetDepthStencilState(nullptr, 0);
        g_pContext->RSSetState(nullptr);
        g_pContext->IASetInputLayout(nullptr);
        g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


        g_pContext->VSSetShader(g_pFilterVS, nullptr, 0);
        g_pContext->PSSetShader(g_pFilterPS, nullptr, 0);


        ID3D11ShaderResourceView* srv[] = { g_pColorBufferSRV };
        g_pContext->PSSetShaderResources(0, 1, srv);

        ID3D11SamplerState* sampler[] = { g_pDefaultSampler };
        g_pContext->PSSetSamplers(0, 1, sampler);

        g_pContext->Draw(3, 0);
    }

    g_pSwapChain->Present(1, 0);
}
void ResizeWindow(UINT newWidth, UINT newHeight)
{
    if (!g_pSwapChain || !g_pDevice || !g_pContext)
        return;

    g_pContext->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pDepthView);

    HRESULT hr = g_pSwapChain->ResizeBuffers(2, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return;

    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (pBackBuffer)
    {
        g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
        pBackBuffer->Release();
    }

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = newWidth;
    depthDesc.Height = newHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencil = nullptr;
    g_pDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (pDepthStencil)
    {
        g_pDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_pDepthView);
        pDepthStencil->Release();
    }

    g_ScreenWidth = newWidth;
    g_ScreenHeight = newHeight;

    CreateColorBuffer(g_ScreenWidth, g_ScreenHeight);
}

void CleanupD3D()
{
    if (g_pContext)
    {
        g_pContext->ClearState();
        g_pContext->Flush();
    }

    SAFE_RELEASE(g_pModelCB);
    SAFE_RELEASE(g_pViewProjCB);
    SAFE_RELEASE(g_pSceneCB);
    SAFE_RELEASE(g_pMainLayout);
    SAFE_RELEASE(g_pMainVS);
    SAFE_RELEASE(g_pMainPS);
    SAFE_RELEASE(g_pSkyboxLayout);
    SAFE_RELEASE(g_pSkyboxVS);
    SAFE_RELEASE(g_pSkyboxPS);
    SAFE_RELEASE(g_pCubeIB);
    SAFE_RELEASE(g_pCubeVB);
    SAFE_RELEASE(g_pSkyboxIB);
    SAFE_RELEASE(g_pSkyboxVB);
    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pDepthView);
    SAFE_RELEASE(g_pSwapChain);
    SAFE_RELEASE(g_pMainTexSRV);
    SAFE_RELEASE(g_pCubemapSRV);
    SAFE_RELEASE(g_pDefaultSampler);
    SAFE_RELEASE(g_pDepthNoWrite);
    SAFE_RELEASE(g_pRSCullBack);
    SAFE_RELEASE(g_pRSCullNone);
    SAFE_RELEASE(g_pNormalMapSRV);

    SAFE_RELEASE(g_pInstancedVS);
    SAFE_RELEASE(g_pInstancedPS);
    SAFE_RELEASE(g_pInstancedLayout);
    SAFE_RELEASE(g_pInstanceBuffer);
    SAFE_RELEASE(g_pVisibleIndicesBuffer);
    SAFE_RELEASE(g_pTextureArraySRV);

    SAFE_RELEASE(g_pColorBufferTex);
    SAFE_RELEASE(g_pColorBufferRTV);
    SAFE_RELEASE(g_pColorBufferSRV);
    SAFE_RELEASE(g_pFilterVS);
    SAFE_RELEASE(g_pFilterPS);

#ifdef _DEBUG
    ID3D11Debug* pDebug = nullptr;
    if (g_pDevice && SUCCEEDED(g_pDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug)))
    {
        // Сначала освободить context
        SAFE_RELEASE(g_pContext);

        // Теперь можно смотреть live objects
        pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
        pDebug->Release();
    }
    else
    {
        SAFE_RELEASE(g_pContext);
    }
#else
    SAFE_RELEASE(g_pContext);
#endif

    SAFE_RELEASE(g_pDevice);
}