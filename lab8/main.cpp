#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>
#include <DirectXMath.h>
#include <cassert>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

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

#define SAFE_RELEASE(p) if ((p) != nullptr) { (p)->Release(); (p) = nullptr; }

std::wstring GetAppPath()
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        path = path.substr(0, pos + 1);
    return path;
}

bool FileExists(const std::wstring& path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring ResolveTexturePath(const std::wstring& relative)
{
    const std::wstring app = GetAppPath();
    const std::wstring candidates[] = {
        app + relative,
        app + L"texture\\" + relative,
        app + L"..\\texture\\" + relative,
        app + L"..\\..\\texture\\" + relative,
        app + L".\\texture\\" + relative,
        app + L".\\.\\texture\\" + relative
    };

    for (const auto& c : candidates)
    {
        if (FileExists(c))
            return c;
    }

    return app + relative;
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
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD dwMagic = 0;
    DWORD dwBytesRead = 0;
    if (!ReadFile(hFile, &dwMagic, sizeof(DWORD), &dwBytesRead, nullptr) || dwMagic != DDS_MAGIC)
    {
        CloseHandle(hFile);
        return false;
    }

    DDS_HEADER header = {};
    if (!ReadFile(hFile, &header, sizeof(DDS_HEADER), &dwBytesRead, nullptr))
    {
        CloseHandle(hFile);
        return false;
    }

    info.width = header.dwWidth;
    info.height = header.dwHeight;
    info.mipLevels = (header.dwSurfaceFlags & DDS_SURFACE_FLAGS_MIPMAP)
        ? static_cast<UINT32>(std::max<DWORD>(1, header.dwMipMapCount))
        : 1u;

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

    const UINT32 blockWidth = DivUp(info.width, 4u);
    const UINT32 blockHeight = DivUp(info.height, 4u);
    info.pitch = blockWidth * GetBytesPerBlock(info.format);
    const UINT32 dataSize = info.pitch * blockHeight;

    info.data = malloc(dataSize);
    if (!info.data)
    {
        CloseHandle(hFile);
        return false;
    }

    if (!ReadFile(hFile, info.data, dataSize, &dwBytesRead, nullptr))
    {
        free(info.data);
        info.data = nullptr;
        CloseHandle(hFile);
        return false;
    }

    CloseHandle(hFile);
    return true;
}

struct SceneConstants
{
    XMFLOAT4X4 viewProj;
    XMFLOAT4 cameraPosition;
    XMFLOAT4 lightCount;
    struct LightSource
    {
        XMFLOAT4 position;
        XMFLOAT4 color;
    } lights[10];
    XMFLOAT4 ambientColor;
};

struct ViewProjectionConstants
{
    XMFLOAT4X4 viewProj;
};

struct InstanceGPUData
{
    XMFLOAT4X4 worldMatrix;
    XMFLOAT4X4 normalMatrix;
    XMFLOAT4 materialProps; // x=shininess, y=rotSpeed, z=texId, w=useNormalMap
    XMFLOAT4 params;        // xyz=center, w=uniformScale
};

struct FrustumCullConstants
{
    XMFLOAT4 planes[6];
    XMUINT4 counts; // x = instanceCount
};

HWND g_hMainWnd = nullptr;

ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* g_pContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pBackBufferRTV = nullptr;
ID3D11DepthStencilView* g_pDepthView = nullptr;

ID3D11Buffer* g_pCubeVB = nullptr;
ID3D11Buffer* g_pCubeIB = nullptr;
ID3D11Buffer* g_pSkyboxVB = nullptr;
ID3D11Buffer* g_pSkyboxIB = nullptr;

ID3D11VertexShader* g_pSkyboxVS = nullptr;
ID3D11PixelShader* g_pSkyboxPS = nullptr;
ID3D11InputLayout* g_pSkyboxLayout = nullptr;

ID3D11VertexShader* g_pInstancedVS = nullptr;
ID3D11PixelShader* g_pInstancedPS = nullptr;
ID3D11InputLayout* g_pInstancedLayout = nullptr;

ID3D11ComputeShader* g_pFrustumCullCS = nullptr;

ID3D11VertexShader* g_pFilterVS = nullptr;
ID3D11PixelShader* g_pFilterPS = nullptr;

ID3D11Buffer* g_pViewProjCB = nullptr;
ID3D11Buffer* g_pSceneCB = nullptr;
ID3D11Buffer* g_pFrustumCullCB = nullptr;

ID3D11Buffer* g_pInstanceStructuredBuffer = nullptr;
ID3D11ShaderResourceView* g_pInstanceSRV = nullptr;

ID3D11Buffer* g_pVisibleIndexBuffer = nullptr;
ID3D11ShaderResourceView* g_pVisibleIndexSRV = nullptr;
ID3D11UnorderedAccessView* g_pVisibleIndexUAV = nullptr;

ID3D11Buffer* g_pVisibleCountBuffer = nullptr;
ID3D11Buffer* g_pVisibleCountReadback = nullptr;

ID3D11ShaderResourceView* g_pCubemapSRV = nullptr;
ID3D11ShaderResourceView* g_pNormalMapSRV = nullptr;
ID3D11ShaderResourceView* g_pTextureArraySRV = nullptr;
ID3D11SamplerState* g_pDefaultSampler = nullptr;

ID3D11DepthStencilState* g_pDepthNoWrite = nullptr;
ID3D11RasterizerState* g_pRSCullBack = nullptr;
ID3D11RasterizerState* g_pRSCullNone = nullptr;

ID3D11Texture2D* g_pColorBufferTex = nullptr;
ID3D11RenderTargetView* g_pColorBufferRTV = nullptr;
ID3D11ShaderResourceView* g_pColorBufferSRV = nullptr;

InstanceGPUData g_InstancesData[MAX_OBJECTS] = {};
UINT g_ActiveInstanceCount = 0;
UINT g_LastVisibleInstanceCount = 0;

UINT g_ScreenWidth = 1280;
UINT g_ScreenHeight = 720;
float g_CameraYaw = 0.0f;
float g_CameraPitch = 0.0f;
float g_CameraDistance = 15.0f;
bool g_KeyLeft = false;
bool g_KeyRight = false;
bool g_KeyUp = false;
bool g_KeyDown = false;
bool g_ApplyFilter = true;
bool g_UsingWarp = false;
double g_PreviousFrameTime = 0.0;

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
bool InitializeD3D();
bool CreateGpuCullingResources();
void CreateGeometryBuffers();
void CompileShaders();
void LoadTextureResources();
void LoadTextureArrayResource();
void CreateInstances();
void UpdateInstanceTransforms(double timeSeconds);
void ExtractFrustumPlanes(const XMMATRIX& vp, XMFLOAT4 outPlanes[6]);
void CreateColorBuffer(UINT width, UINT height);
void ResizeWindow(UINT newWidth, UINT newHeight);
void UpdateCameraMovement(double deltaTime);
void RenderScene();
void CleanupD3D();

void CreateBufferOrDie(const D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA* data, ID3D11Buffer** outBuffer)
{
    HRESULT hr = g_pDevice->CreateBuffer(&desc, data, outBuffer);
    assert(SUCCEEDED(hr));
    (void)hr;
}

void CompileShaderOrDie(
    const char* code,
    const char* entry,
    const char* target,
    ID3DBlob** blobOut)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompile(
        code,
        strlen(code),
        nullptr,
        nullptr,
        nullptr,
        entry,
        target,
        flags,
        0,
        blobOut,
        &errorBlob);

    if (errorBlob)
    {
        OutputDebugStringA((const char*)errorBlob->GetBufferPointer());
        errorBlob->Release();
    }

    assert(SUCCEEDED(hr));
    (void)hr;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"D3D11GpuFrustumCullingLab";

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    RECT rc = { 0, 0, (LONG)g_ScreenWidth, (LONG)g_ScreenHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g_hMainWnd = CreateWindowW(
        wc.lpszClassName,
        L"Graphics Lab: GPU Frustum Culling",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

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

    if (!CreateGpuCullingResources())
    {
        CleanupD3D();
        DestroyWindow(g_hMainWnd);
        return -1;
    }

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
            const UINT newW = LOWORD(lParam);
            const UINT newH = HIWORD(lParam);
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
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtainedLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels,
        1,
        D3D11_SDK_VERSION,
        &scd,
        &g_pSwapChain,
        &g_pDevice,
        &obtainedLevel,
        &g_pContext);

    if (FAILED(hr))
    {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            flags,
            levels,
            1,
            D3D11_SDK_VERSION,
            &scd,
            &g_pSwapChain,
            &g_pDevice,
            &obtainedLevel,
            &g_pContext);
        g_UsingWarp = SUCCEEDED(hr);
    }

    if (FAILED(hr))
        return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr))
        return false;

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    SAFE_RELEASE(pBackBuffer);
    if (FAILED(hr))
        return false;

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = g_ScreenWidth;
    depthDesc.Height = g_ScreenHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* pDepthStencil = nullptr;
    hr = g_pDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil);
    if (FAILED(hr))
        return false;

    hr = g_pDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_pDepthView);
    SAFE_RELEASE(pDepthStencil);
    if (FAILED(hr))
        return false;

    return true;
}

void CreateColorBuffer(UINT width, UINT height)
{
    SAFE_RELEASE(g_pColorBufferTex);
    SAFE_RELEASE(g_pColorBufferRTV);
    SAFE_RELEASE(g_pColorBufferSRV);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = g_pDevice->CreateTexture2D(&desc, nullptr, &g_pColorBufferTex);
    assert(SUCCEEDED(hr));
    hr = g_pDevice->CreateRenderTargetView(g_pColorBufferTex, nullptr, &g_pColorBufferRTV);
    assert(SUCCEEDED(hr));
    hr = g_pDevice->CreateShaderResourceView(g_pColorBufferTex, nullptr, &g_pColorBufferSRV);
    assert(SUCCEEDED(hr));
    (void)hr;
}

bool CreateGpuCullingResources()
{
    HRESULT hr;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(InstanceGPUData) * MAX_OBJECTS;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(InstanceGPUData);

    hr = g_pDevice->CreateBuffer(&bd, nullptr, &g_pInstanceStructuredBuffer);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = MAX_OBJECTS;

    hr = g_pDevice->CreateShaderResourceView(g_pInstanceStructuredBuffer, &srvDesc, &g_pInstanceSRV);
    if (FAILED(hr)) return false;

    ZeroMemory(&bd, sizeof(bd));
    bd.ByteWidth = sizeof(UINT) * MAX_OBJECTS;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(UINT);

    hr = g_pDevice->CreateBuffer(&bd, nullptr, &g_pVisibleIndexBuffer);
    if (FAILED(hr)) return false;

    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = MAX_OBJECTS;

    hr = g_pDevice->CreateShaderResourceView(g_pVisibleIndexBuffer, &srvDesc, &g_pVisibleIndexSRV);
    if (FAILED(hr)) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = MAX_OBJECTS;
    uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;

    hr = g_pDevice->CreateUnorderedAccessView(g_pVisibleIndexBuffer, &uavDesc, &g_pVisibleIndexUAV);
    if (FAILED(hr)) return false;

    ZeroMemory(&bd, sizeof(bd));
    bd.ByteWidth = sizeof(UINT);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = 0;
    hr = g_pDevice->CreateBuffer(&bd, nullptr, &g_pVisibleCountBuffer);
    if (FAILED(hr)) return false;

    ZeroMemory(&bd, sizeof(bd));
    bd.ByteWidth = sizeof(UINT);
    bd.Usage = D3D11_USAGE_STAGING;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    bd.BindFlags = 0;
    hr = g_pDevice->CreateBuffer(&bd, nullptr, &g_pVisibleCountReadback);
    if (FAILED(hr)) return false;

    ZeroMemory(&bd, sizeof(bd));
    bd.ByteWidth = sizeof(FrustumCullConstants);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_pDevice->CreateBuffer(&bd, nullptr, &g_pFrustumCullCB);
    if (FAILED(hr)) return false;

    ZeroMemory(&bd, sizeof(bd));
    bd.ByteWidth = sizeof(ViewProjectionConstants);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_pDevice->CreateBuffer(&bd, nullptr, &g_pViewProjCB);
    if (FAILED(hr)) return false;

    ZeroMemory(&bd, sizeof(bd));
    bd.ByteWidth = sizeof(SceneConstants);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_pDevice->CreateBuffer(&bd, nullptr, &g_pSceneCB);
    if (FAILED(hr)) return false;

    return true;
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
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = cubeVertices;
    CreateBufferOrDie(desc, &data, &g_pCubeVB);

    desc.ByteWidth = sizeof(cubeIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = cubeIndices;
    CreateBufferOrDie(desc, &data, &g_pCubeIB);

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
        0,2,1, 0,3,2,
        4,5,6, 4,6,7,
        0,7,3, 0,4,7,
        1,2,6, 1,6,5,
        3,7,6, 3,6,2,
        0,1,5, 0,5,4
    };

    desc.ByteWidth = sizeof(skyboxVertices);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem = skyboxVertices;
    CreateBufferOrDie(desc, &data, &g_pSkyboxVB);

    desc.ByteWidth = sizeof(skyboxIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = skyboxIndices;
    CreateBufferOrDie(desc, &data, &g_pSkyboxIB);
}

void CompileShaders()
{
    const char* skyboxVS = R"(
        cbuffer ViewProjCB : register(b0)
        {
            float4x4 viewProj;
        };

        struct VSInput
        {
            float3 position : POSITION;
            float2 texCoord : TEXCOORD;
        };

        struct VSOutput
        {
            float4 clipPos : SV_Position;
            float3 localPos : TEXCOORD0;
        };

        VSOutput MainVS(VSInput input)
        {
            VSOutput output;
            output.clipPos = mul(float4(input.position, 1.0), viewProj);
            output.localPos = input.position;
            return output;
        }
    )";

    const char* skyboxPS = R"(
        TextureCube skyboxTexture : register(t0);
        SamplerState skyboxSampler : register(s0);

        struct VSOutput
        {
            float4 clipPos : SV_Position;
            float3 localPos : TEXCOORD0;
        };

        float4 MainPS(VSOutput input) : SV_Target
        {
            return skyboxTexture.Sample(skyboxSampler, input.localPos);
        }
    )";

    const char* instancedVS = R"(
        struct InstanceData
        {
            float4x4 worldMatrix;
            float4x4 normalMatrix;
            float4 materialProps;
            float4 params;
        };

        cbuffer ViewProjCB : register(b0)
        {
            float4x4 viewProj;
        };

        StructuredBuffer<InstanceData> instances : register(t0);
        StructuredBuffer<uint> visibleIds : register(t1);

        struct VSInput
        {
            float3 position : POSITION;
            float3 normal   : NORMAL;
            float3 tangent  : TANGENT;
            float2 texCoord : TEXCOORD;
            uint instanceId : SV_InstanceID;
        };

        struct VSOutput
        {
            float4 clipPos : SV_Position;
            float3 worldPos : TEXCOORD0;
            float3 normal   : TEXCOORD1;
            float3 tangent  : TEXCOORD2;
            float2 texCoord : TEXCOORD3;
            nointerpolation uint realInstanceId : TEXCOORD4;
        };

        VSOutput MainVS(VSInput input)
        {
            VSOutput output;
            uint realId = visibleIds[input.instanceId];
            InstanceData inst = instances[realId];

            float4 worldPos = mul(inst.worldMatrix, float4(input.position, 1.0));
            output.clipPos = mul(worldPos, viewProj);
            output.worldPos = worldPos.xyz;
            output.normal = normalize(mul(inst.normalMatrix, float4(input.normal, 0.0)).xyz);
            output.tangent = normalize(mul(inst.normalMatrix, float4(input.tangent, 0.0)).xyz);
            output.texCoord = input.texCoord;
            output.realInstanceId = realId;
            return output;
        }
    )";

    const char* instancedPS = R"(
        struct InstanceData
        {
            float4x4 worldMatrix;
            float4x4 normalMatrix;
            float4 materialProps;
            float4 params;
        };

        StructuredBuffer<InstanceData> instances : register(t0);
        Texture2DArray colorTexture : register(t2);
        Texture2D normalMapTexture : register(t3);
        SamplerState textureSampler : register(s0);

        cbuffer SceneCB : register(b1)
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

        struct VSOutput
        {
            float4 clipPos : SV_Position;
            float3 worldPos : TEXCOORD0;
            float3 normal   : TEXCOORD1;
            float3 tangent  : TEXCOORD2;
            float2 texCoord : TEXCOORD3;
            nointerpolation uint realInstanceId : TEXCOORD4;
        };

        float4 MainPS(VSOutput input) : SV_Target
        {
            InstanceData inst = instances[input.realInstanceId];
            uint texId = (uint)inst.materialProps.z;

            float3 color = colorTexture.Sample(textureSampler, float3(input.texCoord, texId)).rgb;

            float3 N;
            if ((uint)inst.materialProps.w == 1)
            {
                float3 tangentNormal = normalMapTexture.Sample(textureSampler, input.texCoord).xyz * 2.0 - 1.0;
                float3 n = normalize(input.normal);
                float3 t = normalize(input.tangent);
                float3 b = normalize(cross(n, t));
                N = normalize(tangentNormal.x * t + tangentNormal.y * b + tangentNormal.z * n);
            }
            else
            {
                N = normalize(input.normal);
            }

            float3 finalColor = ambientColor.xyz * color;
            float shininess = inst.materialProps.x;

            [loop]
            for (int i = 0; i < (int)lightCount.x; ++i)
            {
                float3 L = lights[i].position.xyz - input.worldPos;
                float dist = max(length(L), 1e-4);
                L /= dist;

                float attenuation = 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);
                float diffuse = max(dot(N, L), 0.0);
                finalColor += color * diffuse * attenuation * lights[i].color.xyz;

                float3 V = normalize(cameraPos.xyz - input.worldPos);
                float3 R = reflect(-L, N);
                float specular = pow(max(dot(V, R), 0.0), shininess);
                finalColor += specular * attenuation * lights[i].color.xyz;
            }

            return float4(finalColor, 1.0);
        }
    )";

    const char* frustumCullCS = R"(
        struct InstanceData
        {
            float4x4 worldMatrix;
            float4x4 normalMatrix;
            float4 materialProps;
            float4 params;
        };

        StructuredBuffer<InstanceData> instances : register(t0);
        AppendStructuredBuffer<uint> visibleIds : register(u0);

        cbuffer FrustumCullCB : register(b0)
        {
            float4 planes[6];
            uint instanceCount;
            float3 pad;
        };

        [numthreads(64, 1, 1)]
        void MainCS(uint3 tid : SV_DispatchThreadID)
        {
            uint id = tid.x;
            if (id >= instanceCount)
                return;

            float3 center = instances[id].params.xyz;
            float scale = instances[id].params.w;
            float radius = 0.8660254f * scale;

            [unroll]
            for (int i = 0; i < 6; ++i)
            {
                float d = dot(planes[i].xyz, center) + planes[i].w;
                if (d < -radius)
                    return;
            }

            visibleIds.Append(id);
        }
    )";

    const char* filterVS = R"(
        struct VSInput
        {
            uint vertexId : SV_VertexID;
        };

        struct VSOutput
        {
            float4 clipPos : SV_Position;
            float2 texCoord : TEXCOORD0;
        };

        VSOutput MainVS(VSInput input)
        {
            VSOutput output;
            float4 pos;
            switch (input.vertexId)
            {
            case 0: pos = float4(-1,  1, 0, 1); break;
            case 1: pos = float4( 3,  1, 0, 1); break;
            default: pos = float4(-1, -3, 0, 1); break;
            }

            output.clipPos = pos;
            output.texCoord = float2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);
            return output;
        }
    )";

    const char* filterPS = R"(
        Texture2D colorTexture : register(t0);
        SamplerState textureSampler : register(s0);

        struct VSOutput
        {
            float4 clipPos : SV_Position;
            float2 texCoord : TEXCOORD0;
        };

        float4 MainPS(VSOutput input) : SV_Target
        {
            float3 color = colorTexture.Sample(textureSampler, input.texCoord).rgb;
            float gray = dot(color, float3(0.299, 0.587, 0.114));
            return float4(gray, gray, gray, 1.0);
        }
    )";

    ID3DBlob* blob = nullptr;

    CompileShaderOrDie(skyboxVS, "MainVS", "vs_5_0", &blob);
    HRESULT hr = g_pDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pSkyboxVS);
    assert(SUCCEEDED(hr));
    D3D11_INPUT_ELEMENT_DESC skyLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    hr = g_pDevice->CreateInputLayout(skyLayout, 2, blob->GetBufferPointer(), blob->GetBufferSize(), &g_pSkyboxLayout);
    assert(SUCCEEDED(hr));
    SAFE_RELEASE(blob);

    CompileShaderOrDie(skyboxPS, "MainPS", "ps_5_0", &blob);
    hr = g_pDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pSkyboxPS);
    assert(SUCCEEDED(hr));
    SAFE_RELEASE(blob);

    CompileShaderOrDie(instancedVS, "MainVS", "vs_5_0", &blob);
    hr = g_pDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pInstancedVS);
    assert(SUCCEEDED(hr));
    D3D11_INPUT_ELEMENT_DESC instLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    hr = g_pDevice->CreateInputLayout(instLayout, 4, blob->GetBufferPointer(), blob->GetBufferSize(), &g_pInstancedLayout);
    assert(SUCCEEDED(hr));
    SAFE_RELEASE(blob);

    CompileShaderOrDie(instancedPS, "MainPS", "ps_5_0", &blob);
    hr = g_pDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pInstancedPS);
    assert(SUCCEEDED(hr));
    SAFE_RELEASE(blob);

    CompileShaderOrDie(frustumCullCS, "MainCS", "cs_5_0", &blob);
    hr = g_pDevice->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pFrustumCullCS);
    assert(SUCCEEDED(hr));
    SAFE_RELEASE(blob);

    CompileShaderOrDie(filterVS, "MainVS", "vs_5_0", &blob);
    hr = g_pDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pFilterVS);
    assert(SUCCEEDED(hr));
    SAFE_RELEASE(blob);

    CompileShaderOrDie(filterPS, "MainPS", "ps_5_0", &blob);
    hr = g_pDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_pFilterPS);
    assert(SUCCEEDED(hr));
    SAFE_RELEASE(blob);
}

void LoadTextureResources()
{
    HRESULT hr;

    const std::wstring faceNames[6] = {
        ResolveTexturePath(L"skybox\\posx.dds"),
        ResolveTexturePath(L"skybox\\negx.dds"),
        ResolveTexturePath(L"skybox\\posy.dds"),
        ResolveTexturePath(L"skybox\\negy.dds"),
        ResolveTexturePath(L"skybox\\posz.dds"),
        ResolveTexturePath(L"skybox\\negz.dds")
    };

    TextureInfo faceInfos[6] = {};
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
                allOk = false;
                break;
            }
        }
    }

    if (!allOk)
    {
        MessageBoxW(g_hMainWnd, L"Failed to load cubemap or cubemap faces differ in format/size.", L"Texture error", MB_OK | MB_ICONERROR);
    }
    else
    {
        D3D11_TEXTURE2D_DESC cubeDesc = {};
        cubeDesc.Width = faceInfos[0].width;
        cubeDesc.Height = faceInfos[0].height;
        cubeDesc.MipLevels = 1;
        cubeDesc.ArraySize = 6;
        cubeDesc.Format = faceInfos[0].format;
        cubeDesc.SampleDesc.Count = 1;
        cubeDesc.Usage = D3D11_USAGE_IMMUTABLE;
        cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

        D3D11_SUBRESOURCE_DATA initData[6] = {};
        for (int i = 0; i < 6; ++i)
        {
            initData[i].pSysMem = faceInfos[i].data;
            initData[i].SysMemPitch = faceInfos[i].pitch;
        }

        ID3D11Texture2D* cubeTex = nullptr;
        hr = g_pDevice->CreateTexture2D(&cubeDesc, initData, &cubeTex);
        assert(SUCCEEDED(hr));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = cubeDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = 1;
        hr = g_pDevice->CreateShaderResourceView(cubeTex, &srvDesc, &g_pCubemapSRV);
        assert(SUCCEEDED(hr));
        SAFE_RELEASE(cubeTex);
    }

    for (int i = 0; i < 6; ++i)
    {
        if (faceInfos[i].data)
        {
            free(faceInfos[i].data);
            faceInfos[i].data = nullptr;
        }
    }

    TextureInfo normalInfo;
    std::wstring normalPath = ResolveTexturePath(L"BrickNM.dds");
    if (LoadDDSFile(normalPath.c_str(), normalInfo))
    {
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = normalInfo.width;
        texDesc.Height = normalInfo.height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = normalInfo.format;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_IMMUTABLE;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = normalInfo.data;
        initData.SysMemPitch = normalInfo.pitch;

        ID3D11Texture2D* tex = nullptr;
        hr = g_pDevice->CreateTexture2D(&texDesc, &initData, &tex);
        assert(SUCCEEDED(hr));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = texDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr = g_pDevice->CreateShaderResourceView(tex, &srvDesc, &g_pNormalMapSRV);
        assert(SUCCEEDED(hr));
        SAFE_RELEASE(tex);

        free(normalInfo.data);
        normalInfo.data = nullptr;
    }
    else
    {
        OutputDebugStringA("Normal map not found, some materials will render with flat normals.\n");
    }

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = -FLT_MAX;
    sampDesc.MaxLOD = FLT_MAX;
    hr = g_pDevice->CreateSamplerState(&sampDesc, &g_pDefaultSampler);
    assert(SUCCEEDED(hr));

    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = g_pDevice->CreateDepthStencilState(&dsDesc, &g_pDepthNoWrite);
    assert(SUCCEEDED(hr));

    D3D11_RASTERIZER_DESC rsBack = {};
    rsBack.FillMode = D3D11_FILL_SOLID;
    rsBack.CullMode = D3D11_CULL_BACK;
    rsBack.DepthClipEnable = TRUE;
    hr = g_pDevice->CreateRasterizerState(&rsBack, &g_pRSCullBack);
    assert(SUCCEEDED(hr));

    D3D11_RASTERIZER_DESC rsNone = rsBack;
    rsNone.CullMode = D3D11_CULL_NONE;
    hr = g_pDevice->CreateRasterizerState(&rsNone, &g_pRSCullNone);
    assert(SUCCEEDED(hr));
}

void LoadTextureArrayResource()
{
    std::vector<TextureInfo> texInfos(NUM_MATERIALS);
    bool allOk = true;
    for (UINT i = 0; i < NUM_MATERIALS; ++i)
    {
        const std::wstring fullPath = ResolveTexturePath(MATERIAL_PATHS[i]);
        if (!LoadDDSFile(fullPath.c_str(), texInfos[i]))
        {
            allOk = false;
            break;
        }
    }

    if (!allOk)
    {
        MessageBoxW(g_hMainWnd, L"Failed to load one or more material textures.", L"Texture error", MB_OK | MB_ICONERROR);
        return;
    }

    const DXGI_FORMAT fmt = texInfos[0].format;
    const UINT width = texInfos[0].width;
    const UINT height = texInfos[0].height;

    for (UINT i = 1; i < NUM_MATERIALS; ++i)
    {
        if (texInfos[i].format != fmt || texInfos[i].width != width || texInfos[i].height != height)
        {
            MessageBoxW(g_hMainWnd, L"Texture array requires the same format and size for all textures.", L"Texture error", MB_OK | MB_ICONERROR);
            for (auto& t : texInfos) if (t.data) free(t.data);
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

    std::vector<D3D11_SUBRESOURCE_DATA> initData(NUM_MATERIALS);
    for (UINT i = 0; i < NUM_MATERIALS; ++i)
    {
        initData[i].pSysMem = texInfos[i].data;
        initData[i].SysMemPitch = texInfos[i].pitch;
    }

    ID3D11Texture2D* pTexArray = nullptr;
    HRESULT hr = g_pDevice->CreateTexture2D(&texDesc, initData.data(), &pTexArray);
    assert(SUCCEEDED(hr));

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.ArraySize = NUM_MATERIALS;
    hr = g_pDevice->CreateShaderResourceView(pTexArray, &srvDesc, &g_pTextureArraySRV);
    assert(SUCCEEDED(hr));
    SAFE_RELEASE(pTexArray);

    for (auto& td : texInfos)
    {
        if (td.data)
            free(td.data);
        td.data = nullptr;
    }
}

void CreateInstances()
{
    g_ActiveInstanceCount = MAX_OBJECTS;

    const float sizes[MAX_OBJECTS] = { 0.5f, 0.6f, 0.4f, 0.7f, 0.5f, 0.8f, 0.4f, 0.6f, 0.5f, 0.7f };
    const XMFLOAT3 positions[MAX_OBJECTS] = {
        XMFLOAT3(-2.0f,  0.0f, -2.0f),
        XMFLOAT3(2.0f,  0.0f, -2.0f),
        XMFLOAT3(0.0f,  1.0f, -2.0f),
        XMFLOAT3(-2.0f, -1.0f,  0.0f),
        XMFLOAT3(2.0f, -1.0f,  0.0f),
        XMFLOAT3(0.0f,  2.0f,  0.0f),
        XMFLOAT3(-1.5f,  0.5f,  2.0f),
        XMFLOAT3(1.5f,  0.5f,  2.0f),
        XMFLOAT3(0.0f, -0.5f,  3.0f),
        XMFLOAT3(0.0f,  1.5f, -3.0f)
    };

    for (UINT i = 0; i < MAX_OBJECTS; ++i)
    {
        const float size = sizes[i];
        const XMMATRIX scale = XMMatrixScaling(size, size, size);
        const XMMATRIX translation = XMMatrixTranslation(positions[i].x, positions[i].y, positions[i].z);
        const XMMATRIX world = scale * translation;
        const XMMATRIX normal = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

        XMStoreFloat4x4(&g_InstancesData[i].worldMatrix, world);
        XMStoreFloat4x4(&g_InstancesData[i].normalMatrix, normal);

        const int texId = rand() % NUM_MATERIALS;
        const float shininess = 16.0f + (rand() % 48);
        const float rotSpeed = 0.2f + (rand() % 100) / 100.0f;
        const float useNormalMap = (texId == 0 && g_pNormalMapSRV != nullptr) ? 1.0f : 0.0f;

        g_InstancesData[i].materialProps = XMFLOAT4(shininess, rotSpeed, (float)texId, useNormalMap);
        g_InstancesData[i].params = XMFLOAT4(positions[i].x, positions[i].y, positions[i].z, size);
    }
}

void UpdateInstanceTransforms(double timeSeconds)
{
    for (UINT i = 0; i < g_ActiveInstanceCount; ++i)
    {
        const float angle = (float)timeSeconds * g_InstancesData[i].materialProps.y;

        const float px = g_InstancesData[i].params.x;
        const float py = g_InstancesData[i].params.y;
        const float pz = g_InstancesData[i].params.z;
        const float s = g_InstancesData[i].params.w;

        const XMMATRIX scale = XMMatrixScaling(s, s, s);
        const XMMATRIX rotation = XMMatrixRotationY(angle);
        const XMMATRIX translation = XMMatrixTranslation(px, py, pz);

        const XMMATRIX world = scale * rotation * translation;
        const XMMATRIX normal = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

        XMStoreFloat4x4(&g_InstancesData[i].worldMatrix, world);
        XMStoreFloat4x4(&g_InstancesData[i].normalMatrix, normal);
    }
}

void ExtractFrustumPlanes(const XMMATRIX& vp, XMFLOAT4 outPlanes[6])
{
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, vp);

    XMFLOAT4 p[6] = {
        XMFLOAT4(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41),
        XMFLOAT4(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41),
        XMFLOAT4(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42),
        XMFLOAT4(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42),
        XMFLOAT4(m._13,         m._23,         m._33,         m._43),
        XMFLOAT4(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43)
    };

    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR plane = XMLoadFloat4(&p[i]);
        const float len = XMVectorGetX(XMVector3Length(plane));
        outPlanes[i] = XMFLOAT4(p[i].x / len, p[i].y / len, p[i].z / len, p[i].w / len);
    }
}

void UpdateCameraMovement(double deltaTime)
{
    const float speed = 1.0f;
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

    const double currentTime = (double)GetTickCount64() / 1000.0;
    const double deltaTime = currentTime - g_PreviousFrameTime;
    g_PreviousFrameTime = currentTime;

    UpdateCameraMovement(deltaTime);

    g_pContext->ClearState();

    ID3D11ShaderResourceView* nullSRV1[1] = { nullptr };
    g_pContext->PSSetShaderResources(0, 1, nullSRV1);

    ID3D11RenderTargetView* sceneTarget = g_ApplyFilter ? g_pColorBufferRTV : g_pBackBufferRTV;
    g_pContext->OMSetRenderTargets(1, &sceneTarget, g_pDepthView);

    const float clearColor[4] = { 0.10f, 0.12f, 0.15f, 1.0f };
    g_pContext->ClearRenderTargetView(sceneTarget, clearColor);
    g_pContext->ClearDepthStencilView(g_pDepthView, D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT vp = { 0, 0, (FLOAT)g_ScreenWidth, (FLOAT)g_ScreenHeight, 0.0f, 1.0f };
    g_pContext->RSSetViewports(1, &vp);
    g_pContext->RSSetState(g_pRSCullBack);

    const float camX = g_CameraDistance * sinf(g_CameraYaw) * cosf(g_CameraPitch);
    const float camY = g_CameraDistance * sinf(g_CameraPitch);
    const float camZ = g_CameraDistance * cosf(g_CameraYaw) * cosf(g_CameraPitch);

    const XMVECTOR eye = XMVectorSet(camX, camY, camZ, 0.0f);
    const XMVECTOR at = XMVectorZero();
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    const float aspect = (float)g_ScreenWidth / (float)g_ScreenHeight;
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 6.0f, aspect, 0.1f, 25.0f);
    const XMMATRIX viewProj = view * proj;

    // Skybox
    {
        XMMATRIX viewNoTrans = view;
        viewNoTrans.r[3] = XMVectorSet(0, 0, 0, 1);
        const XMMATRIX vpSky = viewNoTrans * proj;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(g_pContext->Map(g_pViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            ViewProjectionConstants* data = (ViewProjectionConstants*)mapped.pData;
            XMStoreFloat4x4(&data->viewProj, XMMatrixTranspose(vpSky));
            g_pContext->Unmap(g_pViewProjCB, 0);
        }

        g_pContext->OMSetDepthStencilState(g_pDepthNoWrite, 0);
        g_pContext->RSSetState(g_pRSCullNone);
        g_pContext->IASetInputLayout(g_pSkyboxLayout);
        g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        UINT stride = sizeof(VertexData);
        UINT offset = 0;
        g_pContext->IASetVertexBuffers(0, 1, &g_pSkyboxVB, &stride, &offset);
        g_pContext->IASetIndexBuffer(g_pSkyboxIB, DXGI_FORMAT_R16_UINT, 0);

        g_pContext->VSSetShader(g_pSkyboxVS, nullptr, 0);
        g_pContext->PSSetShader(g_pSkyboxPS, nullptr, 0);
        g_pContext->VSSetConstantBuffers(0, 1, &g_pViewProjCB);
        g_pContext->PSSetShaderResources(0, 1, &g_pCubemapSRV);
        g_pContext->PSSetSamplers(0, 1, &g_pDefaultSampler);
        g_pContext->DrawIndexed(36, 0, 0);

        ID3D11ShaderResourceView* nullSrv[] = { nullptr };
        g_pContext->PSSetShaderResources(0, 1, nullSrv);
        g_pContext->OMSetDepthStencilState(nullptr, 0);
        g_pContext->RSSetState(g_pRSCullBack);
    }

    // Main matrices
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(g_pContext->Map(g_pViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            ViewProjectionConstants* data = (ViewProjectionConstants*)mapped.pData;
            XMStoreFloat4x4(&data->viewProj, XMMatrixTranspose(viewProj));
            g_pContext->Unmap(g_pViewProjCB, 0);
        }
    }

    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(g_pContext->Map(g_pSceneCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            SceneConstants* scene = (SceneConstants*)mapped.pData;
            XMStoreFloat4x4(&scene->viewProj, XMMatrixTranspose(viewProj));
            scene->cameraPosition = XMFLOAT4(camX, camY, camZ, 1.0f);
            scene->lightCount = XMFLOAT4(4.0f, 1.0f, 0.0f, 0.0f);

            const float t = (float)currentTime;
            scene->lights[0].position = XMFLOAT4(sinf(t) * 3.5f, 1.8f, cosf(t * 0.8f) * 3.0f, 1.0f);
            scene->lights[0].color = XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f);

            scene->lights[1].position = XMFLOAT4(-3.5f, 2.0f, 0.5f, 1.0f);
            scene->lights[1].color = XMFLOAT4(0.2f, 0.4f, 1.0f, 1.0f);

            scene->lights[2].position = XMFLOAT4(3.5f, 1.5f, 1.0f, 1.0f);
            scene->lights[2].color = XMFLOAT4(0.2f, 1.0f, 0.3f, 1.0f);

            scene->lights[3].position = XMFLOAT4(cosf(t * 0.6f) * 3.0f, 1.2f, sinf(t * 0.6f) * 3.0f, 1.0f);
            scene->lights[3].color = XMFLOAT4(1.0f, 0.7f, 0.2f, 1.0f);

            scene->ambientColor = XMFLOAT4(0.25f, 0.28f, 0.35f, 1.0f);
            g_pContext->Unmap(g_pSceneCB, 0);
        }
    }

    UpdateInstanceTransforms(currentTime);
    g_pContext->UpdateSubresource(g_pInstanceStructuredBuffer, 0, nullptr, g_InstancesData, sizeof(InstanceGPUData) * MAX_OBJECTS, 0);

    // GPU frustum culling
    XMFLOAT4 planes[6] = {};
    ExtractFrustumPlanes(viewProj, planes);

    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(g_pContext->Map(g_pFrustumCullCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            FrustumCullConstants* cb = (FrustumCullConstants*)mapped.pData;
            for (int i = 0; i < 6; ++i)
                cb->planes[i] = planes[i];
            cb->counts = XMUINT4(g_ActiveInstanceCount, 0, 0, 0);
            g_pContext->Unmap(g_pFrustumCullCB, 0);
        }
    }

    UINT initialCount = 0;
    g_pContext->CSSetShader(g_pFrustumCullCS, nullptr, 0);
    g_pContext->CSSetConstantBuffers(0, 1, &g_pFrustumCullCB);
    g_pContext->CSSetShaderResources(0, 1, &g_pInstanceSRV);
    g_pContext->CSSetUnorderedAccessViews(0, 1, &g_pVisibleIndexUAV, &initialCount);
    g_pContext->Dispatch((g_ActiveInstanceCount + 63) / 64, 1, 1);

    ID3D11ShaderResourceView* nullCSsrv[1] = { nullptr };
    ID3D11UnorderedAccessView* nullCSuav[1] = { nullptr };
    UINT keepCount = 0xFFFFFFFF;
    g_pContext->CSSetShaderResources(0, 1, nullCSsrv);
    g_pContext->CSSetUnorderedAccessViews(0, 1, nullCSuav, &keepCount);
    g_pContext->CSSetShader(nullptr, nullptr, 0);

    g_pContext->CopyStructureCount(g_pVisibleCountBuffer, 0, g_pVisibleIndexUAV);
    g_pContext->CopyResource(g_pVisibleCountReadback, g_pVisibleCountBuffer);

    g_LastVisibleInstanceCount = 0;
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(g_pContext->Map(g_pVisibleCountReadback, 0, D3D11_MAP_READ, 0, &mapped)))
        {
            g_LastVisibleInstanceCount = *(UINT*)mapped.pData;
            g_pContext->Unmap(g_pVisibleCountReadback, 0);
        }
    }

    // Draw visible instances
    if (g_LastVisibleInstanceCount > 0)
    {
        UINT stride = sizeof(DetailedVertex);
        UINT offset = 0;
        g_pContext->IASetVertexBuffers(0, 1, &g_pCubeVB, &stride, &offset);
        g_pContext->IASetIndexBuffer(g_pCubeIB, DXGI_FORMAT_R16_UINT, 0);
        g_pContext->IASetInputLayout(g_pInstancedLayout);
        g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        g_pContext->VSSetShader(g_pInstancedVS, nullptr, 0);
        g_pContext->PSSetShader(g_pInstancedPS, nullptr, 0);

        g_pContext->VSSetConstantBuffers(0, 1, &g_pViewProjCB);
        g_pContext->PSSetConstantBuffers(1, 1, &g_pSceneCB);

        ID3D11ShaderResourceView* vsSRVs[] = { g_pInstanceSRV, g_pVisibleIndexSRV };
        g_pContext->VSSetShaderResources(0, 2, vsSRVs);

        ID3D11ShaderResourceView* psSRV0[] = { g_pInstanceSRV };
        g_pContext->PSSetShaderResources(0, 1, psSRV0);
        g_pContext->PSSetShaderResources(2, 1, &g_pTextureArraySRV);
        g_pContext->PSSetShaderResources(3, 1, &g_pNormalMapSRV);
        g_pContext->PSSetSamplers(0, 1, &g_pDefaultSampler);

        g_pContext->DrawIndexedInstanced(36, g_LastVisibleInstanceCount, 0, 0, 0);

        ID3D11ShaderResourceView* nullVS[2] = { nullptr, nullptr };
        ID3D11ShaderResourceView* nullPS[4] = { nullptr, nullptr, nullptr, nullptr };
        g_pContext->VSSetShaderResources(0, 2, nullVS);
        g_pContext->PSSetShaderResources(0, 4, nullPS);
    }

    if (g_ApplyFilter)
    {
        g_pContext->OMSetRenderTargets(1, &g_pBackBufferRTV, nullptr);
        g_pContext->ClearRenderTargetView(g_pBackBufferRTV, clearColor);

        g_pContext->RSSetViewports(1, &vp);
        g_pContext->IASetInputLayout(nullptr);
        g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_pContext->VSSetShader(g_pFilterVS, nullptr, 0);
        g_pContext->PSSetShader(g_pFilterPS, nullptr, 0);
        g_pContext->PSSetShaderResources(0, 1, &g_pColorBufferSRV);
        g_pContext->PSSetSamplers(0, 1, &g_pDefaultSampler);
        g_pContext->Draw(3, 0);

        ID3D11ShaderResourceView* nullSrv[] = { nullptr };
        g_pContext->PSSetShaderResources(0, 1, nullSrv);
    }

    wchar_t title[256] = {};
    swprintf_s(
        title,
        L"Graphics Lab: GPU Frustum Culling | visible: %u / %u | %s | filter: %s",
        g_LastVisibleInstanceCount,
        g_ActiveInstanceCount,
        g_UsingWarp ? L"WARP" : L"HARDWARE",
        g_ApplyFilter ? L"ON" : L"OFF");
    SetWindowTextW(g_hMainWnd, title);

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
    if (FAILED(hr))
        return;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (SUCCEEDED(hr))
    {
        hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
        SAFE_RELEASE(pBackBuffer);
        assert(SUCCEEDED(hr));
    }

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = newWidth;
    depthDesc.Height = newHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depthTex = nullptr;
    hr = g_pDevice->CreateTexture2D(&depthDesc, nullptr, &depthTex);
    if (SUCCEEDED(hr))
    {
        hr = g_pDevice->CreateDepthStencilView(depthTex, nullptr, &g_pDepthView);
        SAFE_RELEASE(depthTex);
        assert(SUCCEEDED(hr));
    }

    g_ScreenWidth = newWidth;
    g_ScreenHeight = newHeight;
    CreateColorBuffer(g_ScreenWidth, g_ScreenHeight);
}

void CleanupD3D()
{
    ID3D11Debug* pDebug = nullptr;
#ifdef _DEBUG
    if (g_pDevice)
        g_pDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug);
#endif

    if (g_pContext)
    {
        g_pContext->ClearState();
        g_pContext->Flush();
    }

    SAFE_RELEASE(g_pColorBufferSRV);
    SAFE_RELEASE(g_pColorBufferRTV);
    SAFE_RELEASE(g_pColorBufferTex);

    SAFE_RELEASE(g_pFilterPS);
    SAFE_RELEASE(g_pFilterVS);

    SAFE_RELEASE(g_pVisibleCountReadback);
    SAFE_RELEASE(g_pVisibleCountBuffer);
    SAFE_RELEASE(g_pVisibleIndexUAV);
    SAFE_RELEASE(g_pVisibleIndexSRV);
    SAFE_RELEASE(g_pVisibleIndexBuffer);
    SAFE_RELEASE(g_pInstanceSRV);
    SAFE_RELEASE(g_pInstanceStructuredBuffer);

    SAFE_RELEASE(g_pFrustumCullCB);
    SAFE_RELEASE(g_pFrustumCullCS);
    SAFE_RELEASE(g_pSceneCB);
    SAFE_RELEASE(g_pViewProjCB);

    SAFE_RELEASE(g_pTextureArraySRV);
    SAFE_RELEASE(g_pNormalMapSRV);
    SAFE_RELEASE(g_pCubemapSRV);
    SAFE_RELEASE(g_pDefaultSampler);
    SAFE_RELEASE(g_pDepthNoWrite);
    SAFE_RELEASE(g_pRSCullBack);
    SAFE_RELEASE(g_pRSCullNone);

    SAFE_RELEASE(g_pInstancedLayout);
    SAFE_RELEASE(g_pInstancedPS);
    SAFE_RELEASE(g_pInstancedVS);
    SAFE_RELEASE(g_pSkyboxLayout);
    SAFE_RELEASE(g_pSkyboxPS);
    SAFE_RELEASE(g_pSkyboxVS);

    SAFE_RELEASE(g_pSkyboxIB);
    SAFE_RELEASE(g_pSkyboxVB);
    SAFE_RELEASE(g_pCubeIB);
    SAFE_RELEASE(g_pCubeVB);

    SAFE_RELEASE(g_pDepthView);
    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pSwapChain);
    SAFE_RELEASE(g_pContext);

#ifdef _DEBUG
    if (pDebug)
    {
        pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
        pDebug->Release();
        pDebug = nullptr;
    }
#endif

    SAFE_RELEASE(g_pDevice);
}
