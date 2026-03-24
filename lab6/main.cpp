#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace DirectX;

#define SAFE_RELEASE(p) if (p) { (p)->Release(); (p) = nullptr; }
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#define MAX_POINT_LIGHTS 10

const UINT WINDOW_WIDTH = 1280;
const UINT WINDOW_HEIGHT = 720;

HWND g_hWnd = nullptr;
UINT g_width = WINDOW_WIDTH;
UINT g_height = WINDOW_HEIGHT;

ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* g_pContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pBackBufferRTV = nullptr;
ID3D11DepthStencilView* g_pDepthStencilView = nullptr;

ID3D11Buffer* g_pVertexBuffer = nullptr;
ID3D11Buffer* g_pIndexBuffer = nullptr;
ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;
ID3D11Buffer* g_pModelCB = nullptr;

ID3D11Buffer* g_pSkyboxVertexBuffer = nullptr;
ID3D11Buffer* g_pSkyboxIndexBuffer = nullptr;
ID3D11VertexShader* g_pSkyboxVS = nullptr;
ID3D11PixelShader* g_pSkyboxPS = nullptr;
ID3D11InputLayout* g_pSkyboxInputLayout = nullptr;

ID3D11VertexShader* g_pNormalMapVS = nullptr;
ID3D11PixelShader* g_pNormalMapPS = nullptr;
ID3D11InputLayout* g_pNormalMapInputLayout = nullptr;
ID3D11Buffer* g_pGeomBuffer = nullptr;
ID3D11Buffer* g_pLightCB = nullptr;
ID3D11ShaderResourceView* g_pNormalMapView = nullptr;
ID3D11ShaderResourceView* g_pAlbedoView = nullptr;


ID3D11Buffer* g_pViewProjCB = nullptr;
ID3D11ShaderResourceView* g_pTextureView = nullptr;
ID3D11ShaderResourceView* g_pCubemapView = nullptr;
ID3D11SamplerState* g_pSampler = nullptr;

// Light system
struct PointLight
{
    XMFLOAT4 position;    // xyz = position, w = intensity
    XMFLOAT4 color;       // rgb = color, w = intensity multiplier
    XMFLOAT4 attenuation; // x = constant, y = linear, z = quadratic
};


struct LightConstantBuffer
{
    PointLight lights[MAX_POINT_LIGHTS];
    XMFLOAT4 cameraPosition;
    XMFLOAT4 ambientColor;
    int lightCount;
    float padding[3];
};


struct GeomConstantBuffer
{
    XMMATRIX model;
    XMMATRIX normalMatrix;
    float shininess;
    float padding[3];
};

struct ViewProjConstantBuffer
{
    XMMATRIX vp;
};


struct CubeInstance
{
    XMMATRIX worldMatrix;
    XMFLOAT4 color;
    float shininess;
};

CubeInstance g_cubes[2];

// Camera variables
float g_yaw = 0.0f;
float g_pitch = 0.3f;
float g_distance = 8.0f;
float g_moveSpeed = 1.0f;
double g_lastFrameTime = 0;

bool g_keyLeft = false, g_keyRight = false, g_keyUp = false, g_keyDown = false;


PointLight g_pointLights[MAX_POINT_LIGHTS];
int g_lightCount = 2;
XMFLOAT4 g_ambientColor = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);

struct VertexWithNormalTangent
{
    XMFLOAT3 pos;
    XMFLOAT3 normal;
    XMFLOAT3 tangent;
    XMFLOAT2 uv;
};

struct TexturedVertex
{
    XMFLOAT3 pos;
    XMFLOAT2 uv;
};


#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
     ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

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

struct TextureDesc
{
    UINT32 pitch = 0;
    UINT32 mipmapsCount = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    UINT32 width = 0;
    UINT32 height = 0;
    void* pData = nullptr;
};

//=====================================================================
// FORWARD DECLARATIONS
//=====================================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
bool Initialize(HWND hWnd, UINT width, UINT height);
void Cleanup();
void Render();
void Resize(UINT newWidth, UINT newHeight);
void HandleKey(UINT key, bool isDown);

bool CreateDeviceAndSwapChain();
bool CreateRenderTargetAndDepthStencil();
bool CreateBuffers();
bool CompileShaders();
bool LoadTextures();
void SetupCubes();
void SetupPointLights();

void UpdateCamera(float deltaTime);
void UpdateLights(float time);
void RenderSkybox(const XMMATRIX& vpSky);
void RenderCubes(const XMMATRIX& view, const XMMATRIX& proj, float time);

UINT GetBytesPerBlock(DXGI_FORMAT fmt);
bool LoadDDS(const wchar_t* filename, TextureDesc& desc);
ID3D11ShaderResourceView* CreateTexture2D(ID3D11Device* device, const TextureDesc& desc);
ID3D11ShaderResourceView* CreateCubemap(ID3D11Device* device, const std::wstring* facePaths);

std::wstring GetPath();

//=====================================================================
// IMPLEMENTATIONS
//=====================================================================

std::wstring GetPath()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        path = path.substr(0, pos + 1);
    return path;
}

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

bool LoadDDS(const wchar_t* filename, TextureDesc& desc)
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

    desc.width = header.dwWidth;
    desc.height = header.dwHeight;
    desc.mipmapsCount = (header.dwSurfaceFlags & DDS_SURFACE_FLAGS_MIPMAP) ?
        max(1, header.dwMipMapCount) : 1;

    if (header.ddspf.dwFlags & DDS_FOURCC)
    {
        switch (header.ddspf.dwFourCC)
        {
        case FOURCC_DXT1: desc.fmt = DXGI_FORMAT_BC1_UNORM; break;
        case FOURCC_DXT3: desc.fmt = DXGI_FORMAT_BC2_UNORM; break;
        case FOURCC_DXT5: desc.fmt = DXGI_FORMAT_BC3_UNORM; break;
        default: desc.fmt = DXGI_FORMAT_UNKNOWN; break;
        }
    }
    else if (header.ddspf.dwFlags & DDS_RGB)
    {
        if (header.ddspf.dwRGBBitCount == 32)
        {
            if (header.ddspf.dwABitMask != 0)
                desc.fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
            else
                desc.fmt = DXGI_FORMAT_B8G8R8X8_UNORM;
        }
        else if (header.ddspf.dwRGBBitCount == 24)
            desc.fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        else
            desc.fmt = DXGI_FORMAT_UNKNOWN;
    }

    if (desc.fmt == DXGI_FORMAT_UNKNOWN)
    {
        CloseHandle(hFile);
        return false;
    }

    UINT totalSize = 0;
    UINT width = desc.width;
    UINT height = desc.height;

    for (UINT i = 0; i < desc.mipmapsCount; ++i)
    {
        UINT blockWidth = (width + 3) / 4;
        UINT blockHeight = (height + 3) / 4;
        UINT mipSize = blockWidth * blockHeight * GetBytesPerBlock(desc.fmt);
        totalSize += mipSize;
        width = max(1, width / 2);
        height = max(1, height / 2);
    }

    desc.pData = malloc(totalSize);
    if (!desc.pData)
    {
        CloseHandle(hFile);
        return false;
    }

    BYTE* pDataPtr = (BYTE*)desc.pData;
    width = desc.width;
    height = desc.height;

    for (UINT i = 0; i < desc.mipmapsCount; ++i)
    {
        UINT blockWidth = (width + 3) / 4;
        UINT blockHeight = (height + 3) / 4;
        UINT mipSize = blockWidth * blockHeight * GetBytesPerBlock(desc.fmt);

        ReadFile(hFile, pDataPtr, mipSize, &dwBytesRead, NULL);
        pDataPtr += mipSize;

        width = max(1, width / 2);
        height = max(1, height / 2);
    }

    CloseHandle(hFile);
    return true;
}

ID3D11ShaderResourceView* CreateTexture2D(ID3D11Device* device, const TextureDesc& desc)
{
    D3D11_TEXTURE2D_DESC tex2DDesc = {};
    tex2DDesc.Width = desc.width;
    tex2DDesc.Height = desc.height;
    tex2DDesc.MipLevels = desc.mipmapsCount;
    tex2DDesc.ArraySize = 1;
    tex2DDesc.Format = desc.fmt;
    tex2DDesc.SampleDesc.Count = 1;
    tex2DDesc.SampleDesc.Quality = 0;
    tex2DDesc.Usage = D3D11_USAGE_IMMUTABLE;
    tex2DDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<D3D11_SUBRESOURCE_DATA> initData(desc.mipmapsCount);
    BYTE* pDataPtr = (BYTE*)desc.pData;
    UINT width = desc.width;
    UINT height = desc.height;

    for (UINT i = 0; i < desc.mipmapsCount; ++i)
    {
        UINT blockWidth = (width + 3) / 4;
        UINT blockHeight = (height + 3) / 4;
        UINT pitch = blockWidth * GetBytesPerBlock(desc.fmt);

        initData[i].pSysMem = pDataPtr;
        initData[i].SysMemPitch = pitch;
        initData[i].SysMemSlicePitch = 0;

        pDataPtr += pitch * blockHeight;
        width = max(1, width / 2);
        height = max(1, height / 2);
    }

    ID3D11Texture2D* pTexture = nullptr;
    ID3D11ShaderResourceView* pSRV = nullptr;

    if (FAILED(device->CreateTexture2D(&tex2DDesc, initData.data(), &pTexture)))
        return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.fmt;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.mipmapsCount;
    srvDesc.Texture2D.MostDetailedMip = 0;

    device->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
    pTexture->Release();
    return pSRV;
}

ID3D11ShaderResourceView* CreateCubemap(ID3D11Device* device, const std::wstring* facePaths)
{
    TextureDesc faceDescs[6];
    bool allOk = true;
    UINT mipCount = 0;

    for (int i = 0; i < 6; ++i)
    {
        if (!LoadDDS(facePaths[i].c_str(), faceDescs[i]))
        {
            allOk = false;
            break;
        }
        if (i == 0) mipCount = faceDescs[i].mipmapsCount;
    }

    if (!allOk)
    {
        for (int i = 0; i < 6; ++i)
            if (faceDescs[i].pData) free(faceDescs[i].pData);
        return nullptr;
    }

    for (int i = 1; i < 6; ++i)
    {
        if (faceDescs[i].fmt != faceDescs[0].fmt ||
            faceDescs[i].width != faceDescs[0].width ||
            faceDescs[i].height != faceDescs[0].height ||
            faceDescs[i].mipmapsCount != mipCount)
        {
            for (int j = 0; j < 6; ++j) free(faceDescs[j].pData);
            return nullptr;
        }
    }

    D3D11_TEXTURE2D_DESC cubeDesc = {};
    cubeDesc.Width = faceDescs[0].width;
    cubeDesc.Height = faceDescs[0].height;
    cubeDesc.MipLevels = mipCount;
    cubeDesc.ArraySize = 6;
    cubeDesc.Format = faceDescs[0].fmt;
    cubeDesc.SampleDesc.Count = 1;
    cubeDesc.SampleDesc.Quality = 0;
    cubeDesc.Usage = D3D11_USAGE_IMMUTABLE;
    cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    std::vector<D3D11_SUBRESOURCE_DATA> cubeInitData(6 * mipCount);

    for (int face = 0; face < 6; ++face)
    {
        BYTE* pFaceData = (BYTE*)faceDescs[face].pData;
        UINT faceWidth = faceDescs[face].width;
        UINT faceHeight = faceDescs[face].height;

        for (UINT mip = 0; mip < mipCount; ++mip)
        {
            UINT blockWidth = (faceWidth + 3) / 4;
            UINT blockHeight = (faceHeight + 3) / 4;
            UINT pitch = blockWidth * GetBytesPerBlock(cubeDesc.Format);

            cubeInitData[face * mipCount + mip].pSysMem = pFaceData;
            cubeInitData[face * mipCount + mip].SysMemPitch = pitch;
            cubeInitData[face * mipCount + mip].SysMemSlicePitch = 0;

            pFaceData += pitch * blockHeight;
            faceWidth = max(1, faceWidth / 2);
            faceHeight = max(1, faceHeight / 2);
        }
    }

    ID3D11Texture2D* pCubemapTex = nullptr;
    ID3D11ShaderResourceView* pSRV = nullptr;

    if (FAILED(device->CreateTexture2D(&cubeDesc, cubeInitData.data(), &pCubemapTex)))
    {
        for (int i = 0; i < 6; ++i) free(faceDescs[i].pData);
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC cubeSRVDesc = {};
    cubeSRVDesc.Format = cubeDesc.Format;
    cubeSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    cubeSRVDesc.TextureCube.MipLevels = mipCount;
    cubeSRVDesc.TextureCube.MostDetailedMip = 0;

    device->CreateShaderResourceView(pCubemapTex, &cubeSRVDesc, &pSRV);
    pCubemapTex->Release();

    for (int i = 0; i < 6; ++i) free(faceDescs[i].pData);
    return pSRV;
}

void SetupCubes()
{
    // Cube 1 - left side
    g_cubes[0].worldMatrix = XMMatrixTranslation(-2.5f, 0.0f, 0.0f);
    g_cubes[0].color = XMFLOAT4(1.0f, 0.5f, 0.2f, 1.0f);
    g_cubes[0].shininess = 32.0f;  

    // Cube 2 - right side
    g_cubes[1].worldMatrix = XMMatrixTranslation(2.5f, 0.0f, 0.0f);
    g_cubes[1].color = XMFLOAT4(0.2f, 0.5f, 1.0f, 1.0f);
    g_cubes[1].shininess = 64.0f;  
}

void SetupPointLights()
{
    // Light 1 - left side
    g_pointLights[0].position = XMFLOAT4(-2.5f, 3.0f, 2.0f, 1.0f);
    g_pointLights[0].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 2.5f);
    g_pointLights[0].attenuation = XMFLOAT4(1.0f, 0.2f, 0.05f, 0.0f);

    // Light 2 - right side
    g_pointLights[1].position = XMFLOAT4(2.5f, 3.0f, 2.0f, 1.0f);
    g_pointLights[1].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 2.5f);
    g_pointLights[1].attenuation = XMFLOAT4(1.0f, 0.2f, 0.05f, 0.0f);

    g_lightCount = 2;
}

void UpdateLights(float time)
{
    g_pointLights[0].position.x = -2.5f;
    g_pointLights[0].position.z = 2.0f;
    g_pointLights[0].position.y = 3.0f;

    g_pointLights[1].position.x = 2.5f;
    g_pointLights[1].position.z = 2.0f;
    g_pointLights[1].position.y = 3.0f;
}

void UpdateCamera(float deltaTime)
{
    if (g_keyLeft) g_yaw -= g_moveSpeed * deltaTime;
    if (g_keyRight) g_yaw += g_moveSpeed * deltaTime;
    if (g_keyUp) g_pitch += g_moveSpeed * deltaTime;
    if (g_keyDown) g_pitch -= g_moveSpeed * deltaTime;

    const float maxPitch = 1.5f;
    if (g_pitch > maxPitch) g_pitch = maxPitch;
    if (g_pitch < -maxPitch) g_pitch = -maxPitch;
}

XMMATRIX GetViewMatrix()
{
    float camX = g_distance * sin(g_yaw) * cos(g_pitch);
    float camY = g_distance * sin(g_pitch);
    float camZ = g_distance * cos(g_yaw) * cos(g_pitch);

    XMVECTOR eye = XMVectorSet(camX, camY, camZ, 0.0f);
    XMVECTOR at = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    return XMMatrixLookAtLH(eye, at, up);
}

XMMATRIX GetViewNoTranslationMatrix()
{
    XMMATRIX view = GetViewMatrix();
    view.r[3] = XMVectorSet(0, 0, 0, 1);
    return view;
}

XMVECTOR GetEyePosition()
{
    return XMVectorSet(
        g_distance * sin(g_yaw) * cos(g_pitch),
        g_distance * sin(g_pitch),
        g_distance * cos(g_yaw) * cos(g_pitch),
        0.0f
    );
}

bool CreateDeviceAndSwapChain()
{
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = g_width;
    scd.BufferDesc.Height = g_height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = g_hWnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtainedLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, levels, 1, D3D11_SDK_VERSION,
        &scd, &g_pSwapChain, &g_pDevice, &obtainedLevel, &g_pContext
    );

    return SUCCEEDED(hr);
}

bool CreateRenderTargetAndDepthStencil()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) return false;

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pBackBufferRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = g_width;
    depthDesc.Height = g_height;
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

    hr = g_pDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_pDepthStencilView);
    pDepthStencil->Release();

    return SUCCEEDED(hr);
}

bool CreateBuffers()
{

    const VertexWithNormalTangent cubeVertices[] = {
        // Front face
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(0.0f, 0.0f) },
        // Back face
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0,0,1), XMFLOAT3(-1,0,0), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(0,0,1), XMFLOAT3(-1,0,0), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(0,0,1), XMFLOAT3(-1,0,0), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0,0,1), XMFLOAT3(-1,0,0), XMFLOAT2(0.0f, 0.0f) },
        // Left face
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,-1), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,-1), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,-1), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,-1), XMFLOAT2(0.0f, 0.0f) },
        // Right face
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0.0f, 0.0f) },
        // Top face
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(0.0f, 0.0f) },
        // Bottom face
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0,-1,0), XMFLOAT3(1,0,0), XMFLOAT2(0.0f, 0.0f) }
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
    if (FAILED(g_pDevice->CreateBuffer(&desc, &data, &g_pVertexBuffer)))
        return false;

    desc.ByteWidth = sizeof(cubeIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = cubeIndices;
    if (FAILED(g_pDevice->CreateBuffer(&desc, &data, &g_pIndexBuffer)))
        return false;

    desc.ByteWidth = sizeof(skyboxVertices);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem = skyboxVertices;
    if (FAILED(g_pDevice->CreateBuffer(&desc, &data, &g_pSkyboxVertexBuffer)))
        return false;

    desc.ByteWidth = sizeof(skyboxIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = skyboxIndices;
    if (FAILED(g_pDevice->CreateBuffer(&desc, &data, &g_pSkyboxIndexBuffer)))
        return false;

    desc = {};
    desc.ByteWidth = sizeof(GeomConstantBuffer);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(g_pDevice->CreateBuffer(&desc, nullptr, &g_pGeomBuffer)))
        return false;

    desc.ByteWidth = sizeof(ViewProjConstantBuffer);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_pDevice->CreateBuffer(&desc, nullptr, &g_pViewProjCB)))
        return false;

    desc.ByteWidth = sizeof(LightConstantBuffer);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_pDevice->CreateBuffer(&desc, nullptr, &g_pLightCB)))
        return false;

    return true;
}

bool CompileShaders()
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pVsBlob = nullptr;
    ID3DBlob* pPsBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;

    const char* normalMapVS = R"(
        cbuffer GeomBuffer : register(b0) {
            float4x4 model;
            float4x4 normalMatrix;
            float shininess;
        }
        cbuffer ViewProjCB : register(b1) { 
            float4x4 vp; 
        }
        
        struct VSInput {
            float3 pos : POSITION;
            float3 normal : NORMAL;
            float3 tangent : TANGENT;
            float2 uv : TEXCOORD;
        };
        
        struct VSOutput {
            float4 pos : SV_Position;
            float4 worldPos : POSITION;
            float3 norm : NORMAL;
            float3 tang : TANGENT;
            float2 uv : TEXCOORD;
        };
        
        VSOutput vs(VSInput v) {
            VSOutput o;
            float4 worldPos = mul(float4(v.pos, 1.0), model);
            o.pos = mul(worldPos, vp);
            o.worldPos = worldPos;
            o.norm = mul(v.normal, (float3x3)normalMatrix);
            o.tang = mul(v.tangent, (float3x3)normalMatrix);
            o.uv = v.uv;
            return o;
        }
    )";

    const char* normalMapPS = R"(
        cbuffer LightCB : register(b2) {
            struct PointLight {
                float4 position;
                float4 color;
                float4 attenuation;
            };
            PointLight lights[10];
            float4 cameraPosition;
            float4 ambientColor;
            int lightCount;
        };
        
        cbuffer GeomBuffer : register(b1) {
            float4x4 model;
            float4x4 normalMatrix;
            float shininess;
        };
        
        Texture2D albedoTexture : register(t0);
        Texture2D normalTexture : register(t1);
        SamplerState colorSampler : register(s0);
        
        struct VSOutput {
            float4 pos : SV_Position;
            float4 worldPos : POSITION;
            float3 norm : NORMAL;
            float3 tang : TANGENT;
            float2 uv : TEXCOORD;
        };
        
        float4 ps(VSOutput pixel) : SV_Target0 {
            
            float3 albedo = albedoTexture.Sample(colorSampler, pixel.uv).xyz;
            
            float3 finalColor = ambientColor.xyz * albedo;
            
            float3 N = normalize(pixel.norm);
            float3 T = normalize(pixel.tang);
            float3 B = cross(N, T);
            float3x3 TBN = float3x3(T, B, N);
            
          
            float3 sampledNormal = normalTexture.Sample(colorSampler, pixel.uv).xyz * 2.0 - 1.0;
            float3 normal = normalize(mul(sampledNormal, TBN));
            
            for (int i = 0; i < lightCount; i++) {
                float3 lightDir = lights[i].position.xyz - pixel.worldPos.xyz;
                float lightDist = length(lightDir);
                lightDir = lightDist > 0 ? lightDir / lightDist : float3(0,0,0);
                
                float attenuation = 1.0 / (lights[i].attenuation.x + 
                                           lights[i].attenuation.y * lightDist + 
                                           lights[i].attenuation.z * lightDist * lightDist);
                
                
                float NdotL = max(dot(lightDir, normal), 0);
                finalColor += albedo * NdotL * attenuation * lights[i].color.xyz;
                
              
                float3 viewDir = normalize(cameraPosition.xyz - pixel.worldPos.xyz);
                float3 reflectDir = reflect(-lightDir, normal);
                float spec = shininess > 0 ? pow(max(dot(viewDir, reflectDir), 0.0), shininess) : 0.0;
                finalColor += albedo * spec * lights[i].color.xyz;
            }
            
            return float4(finalColor, 1.0);
        }
    )";


    if (FAILED(D3DCompile(normalMapVS, strlen(normalMapVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob))) {
        if (pErrorBlob) OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
        SAFE_RELEASE(pErrorBlob);
        return false;
    }
    g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pNormalMapVS);


    if (FAILED(D3DCompile(normalMapPS, strlen(normalMapPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob))) {
        if (pErrorBlob) OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
        SAFE_RELEASE(pErrorBlob);
        return false;
    }
    g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pNormalMapPS);


    D3D11_INPUT_ELEMENT_DESC normalMapLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_pDevice->CreateInputLayout(normalMapLayout, 4, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pNormalMapInputLayout);

    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);
    SAFE_RELEASE(pErrorBlob);

    // Skybox vertex shader
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

    pVsBlob = pPsBlob = pErrorBlob = nullptr;
    if (FAILED(D3DCompile(skyboxVS, strlen(skyboxVS), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVsBlob, &pErrorBlob))) {
        if (pErrorBlob) OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
        SAFE_RELEASE(pErrorBlob);
        return false;
    }
    g_pDevice->CreateVertexShader(pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), nullptr, &g_pSkyboxVS);

    if (FAILED(D3DCompile(skyboxPS, strlen(skyboxPS), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPsBlob, &pErrorBlob))) {
        if (pErrorBlob) OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
        SAFE_RELEASE(pErrorBlob);
        return false;
    }
    g_pDevice->CreatePixelShader(pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize(), nullptr, &g_pSkyboxPS);

    D3D11_INPUT_ELEMENT_DESC skyboxLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    g_pDevice->CreateInputLayout(skyboxLayout, 2, pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize(), &g_pSkyboxInputLayout);

    SAFE_RELEASE(pVsBlob);
    SAFE_RELEASE(pPsBlob);
    SAFE_RELEASE(pErrorBlob);

    return true;
}

bool LoadTextures()
{
    TextureDesc texDesc;
    std::wstring fullPath = GetPath() + L"..\\..\\texture\\wood.dds";
    if (!LoadDDS(fullPath.c_str(), texDesc))
    {
        MessageBoxA(NULL, "Failed to load wood.dds", "Error", MB_OK);
        return false;
    }

    g_pAlbedoView = CreateTexture2D(g_pDevice, texDesc);
    free(texDesc.pData);

    if (!g_pAlbedoView)
    {
        MessageBoxA(NULL, "Failed to create albedo texture SRV", "Error", MB_OK);
        return false;
    }

    // Load normal map
    std::wstring normalPath = GetPath() + L"..\\..\\texture\\wood02_normal.dds";
    if (LoadDDS(normalPath.c_str(), texDesc))
    {
        g_pNormalMapView = CreateTexture2D(g_pDevice, texDesc);
        free(texDesc.pData);

        if (!g_pNormalMapView)
        {
            MessageBoxA(NULL, "Failed to create normal map SRV", "Error", MB_OK);
            g_pNormalMapView = g_pAlbedoView;
            g_pNormalMapView->AddRef();
        }
    }
    else
    {
        MessageBoxA(NULL, "Normal map not found, using albedo as fallback", "Warning", MB_OK);
        g_pNormalMapView = g_pAlbedoView;
        g_pNormalMapView->AddRef();
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

    if (FAILED(g_pDevice->CreateSamplerState(&sampDesc, &g_pSampler)))
    {
        MessageBoxA(NULL, "Failed to create sampler", "Error", MB_OK);
        return false;
    }

    // Load cubemap
    std::wstring path = GetPath() + L"..\\..\\texture\\skybox\\";
    std::wstring faceNames[6] = {
        path + L"posx.dds", path + L"negx.dds",
        path + L"posy.dds", path + L"negy.dds",
        path + L"posz.dds", path + L"negz.dds"
    };

    g_pCubemapView = CreateCubemap(g_pDevice, faceNames);
    if (!g_pCubemapView)
    {
        MessageBoxA(NULL, "Failed to load cubemap", "Error", MB_OK);
        return false;
    }

    return true;
}

void RenderSkybox(const XMMATRIX& vpSky)
{
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dsDesc.StencilEnable = FALSE;
    ID3D11DepthStencilState* pDSSky = nullptr;
    g_pDevice->CreateDepthStencilState(&dsDesc, &pDSSky);
    g_pContext->OMSetDepthStencilState(pDSSky, 0);

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    ID3D11RasterizerState* pRSSky = nullptr;
    g_pDevice->CreateRasterizerState(&rsDesc, &pRSSky);
    g_pContext->RSSetState(pRSSky);

    g_pContext->VSSetShader(g_pSkyboxVS, nullptr, 0);
    g_pContext->PSSetShader(g_pSkyboxPS, nullptr, 0);
    g_pContext->IASetInputLayout(g_pSkyboxInputLayout);

    UINT stride = sizeof(TexturedVertex);
    UINT offset = 0;
    ID3D11Buffer* vbSky[] = { g_pSkyboxVertexBuffer };
    g_pContext->IASetVertexBuffers(0, 1, vbSky, &stride, &offset);
    g_pContext->IASetIndexBuffer(g_pSkyboxIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11Buffer* cbsSky[] = { nullptr, g_pViewProjCB };
    g_pContext->VSSetConstantBuffers(0, 2, cbsSky);

    ID3D11ShaderResourceView* skySRV[] = { g_pCubemapView };
    g_pContext->PSSetShaderResources(1, 1, skySRV);
    ID3D11SamplerState* samplers[] = { g_pSampler };
    g_pContext->PSSetSamplers(1, 1, samplers);

    g_pContext->DrawIndexed(36, 0, 0);

    SAFE_RELEASE(pRSSky);
    SAFE_RELEASE(pDSSky);
}

void RenderCubes(const XMMATRIX& view, const XMMATRIX& proj, float time)
{
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
    dsDesc.StencilEnable = FALSE;
    ID3D11DepthStencilState* pDSCube = nullptr;
    g_pDevice->CreateDepthStencilState(&dsDesc, &pDSCube);
    g_pContext->OMSetDepthStencilState(pDSCube, 0);

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    ID3D11RasterizerState* pRSCube = nullptr;
    g_pDevice->CreateRasterizerState(&rsDesc, &pRSCube);
    g_pContext->RSSetState(pRSCube);

    XMMATRIX vp = view * proj;

    // Update view-proj buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_pContext->Map(g_pViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        ViewProjConstantBuffer* pData = (ViewProjConstantBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(vp));
        g_pContext->Unmap(g_pViewProjCB, 0);
    }

    LightConstantBuffer lightData;
    for (int l = 0; l < MAX_POINT_LIGHTS; l++)
    {
        lightData.lights[l] = g_pointLights[l];
    }
    lightData.lightCount = g_lightCount;
    lightData.ambientColor = g_ambientColor;
    XMVECTOR eyePos = GetEyePosition();
    XMStoreFloat4(&lightData.cameraPosition, eyePos);

    if (SUCCEEDED(g_pContext->Map(g_pLightCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &lightData, sizeof(LightConstantBuffer));
        g_pContext->Unmap(g_pLightCB, 0);
    }

    static float rotations[2] = { 0.0f, 0.0f };

    // Set common shader resources
    g_pContext->VSSetShader(g_pNormalMapVS, nullptr, 0);
    g_pContext->PSSetShader(g_pNormalMapPS, nullptr, 0);
    g_pContext->IASetInputLayout(g_pNormalMapInputLayout);

    UINT stride = sizeof(VertexWithNormalTangent);
    UINT offset = 0;
    ID3D11Buffer* vbCube[] = { g_pVertexBuffer };
    g_pContext->IASetVertexBuffers(0, 1, vbCube, &stride, &offset);
    g_pContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11ShaderResourceView* cubeSRV[] = { g_pAlbedoView, g_pNormalMapView };
    g_pContext->PSSetShaderResources(0, 2, cubeSRV);
    ID3D11SamplerState* samplers[] = { g_pSampler };
    g_pContext->PSSetSamplers(0, 1, samplers);

    g_pContext->PSSetConstantBuffers(2, 1, &g_pLightCB);


    for (int i = 0; i < 2; i++)
    {

        rotations[i] += time * 0.8f;
        XMMATRIX model = g_cubes[i].worldMatrix * XMMatrixRotationY(rotations[i]);
        XMMATRIX normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, model));

        GeomConstantBuffer geomData;
        XMStoreFloat4x4((XMFLOAT4X4*)&geomData.model, XMMatrixTranspose(model));
        XMStoreFloat4x4((XMFLOAT4X4*)&geomData.normalMatrix, XMMatrixTranspose(normalMatrix));
        geomData.shininess = g_cubes[i].shininess;
        g_pContext->UpdateSubresource(g_pGeomBuffer, 0, nullptr, &geomData, 0, 0);


        ID3D11Buffer* cbsCube[] = { g_pGeomBuffer, g_pViewProjCB };
        g_pContext->VSSetConstantBuffers(0, 2, cbsCube);
        g_pContext->PSSetConstantBuffers(1, 1, &g_pGeomBuffer);

        g_pContext->DrawIndexed(36, 0, 0);
    }

    SAFE_RELEASE(pRSCube);
    SAFE_RELEASE(pDSCube);
}

void Render()
{
    if (!g_pContext || !g_pBackBufferRTV || !g_pSwapChain)
        return;

    double currentTime = (double)GetTickCount64() / 1000.0;
    float deltaTime = (float)(currentTime - g_lastFrameTime);
    g_lastFrameTime = currentTime;

    UpdateCamera(deltaTime);
    UpdateLights((float)currentTime);

    g_pContext->OMSetRenderTargets(1, &g_pBackBufferRTV, g_pDepthStencilView);
    const float clearColor[4] = { 0.05f, 0.05f, 0.1f, 1.0f };
    g_pContext->ClearRenderTargetView(g_pBackBufferRTV, clearColor);
    g_pContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT vpView = { 0, 0, (float)g_width, (float)g_height, 0.0f, 1.0f };
    g_pContext->RSSetViewports(1, &vpView);

    XMMATRIX view = GetViewMatrix();
    XMMATRIX viewNoTrans = GetViewNoTranslationMatrix();
    float aspect = (float)g_width / (float)g_height;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3.0f, aspect, 0.1f, 100.0f);
    XMMATRIX vpSky = viewNoTrans * proj;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_pContext->Map(g_pViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        ViewProjConstantBuffer* pData = (ViewProjConstantBuffer*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(vpSky));
        g_pContext->Unmap(g_pViewProjCB, 0);
    }

    RenderSkybox(vpSky);

    RenderCubes(view, proj, deltaTime);

    g_pSwapChain->Present(1, 0);
}

void Resize(UINT newWidth, UINT newHeight)
{
    if (!g_pSwapChain || !g_pDevice || !g_pContext || newWidth == 0 || newHeight == 0)
        return;

    g_pContext->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pDepthStencilView);

    HRESULT hr = g_pSwapChain->ResizeBuffers(2, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return;

    ID3D11Texture2D* pBackBuffer = nullptr;
    if (SUCCEEDED(g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer)))
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
    if (SUCCEEDED(g_pDevice->CreateTexture2D(&depthDesc, nullptr, &pDepthStencil)))
    {
        g_pDevice->CreateDepthStencilView(pDepthStencil, nullptr, &g_pDepthStencilView);
        pDepthStencil->Release();
    }

    g_width = newWidth;
    g_height = newHeight;
}

void HandleKey(UINT key, bool isDown)
{
    switch (key)
    {
    case VK_LEFT: g_keyLeft = isDown; break;
    case VK_RIGHT: g_keyRight = isDown; break;
    case VK_UP: g_keyUp = isDown; break;
    case VK_DOWN: g_keyDown = isDown; break;
    }
}

void Cleanup()
{
    if (g_pContext)
        g_pContext->ClearState();

    SAFE_RELEASE(g_pGeomBuffer);
    SAFE_RELEASE(g_pLightCB);
    SAFE_RELEASE(g_pNormalMapView);
    SAFE_RELEASE(g_pAlbedoView);
    SAFE_RELEASE(g_pModelCB);
    SAFE_RELEASE(g_pViewProjCB);

    SAFE_RELEASE(g_pInputLayout);
    SAFE_RELEASE(g_pVertexShader);
    SAFE_RELEASE(g_pPixelShader);

    SAFE_RELEASE(g_pNormalMapInputLayout);
    SAFE_RELEASE(g_pNormalMapVS);
    SAFE_RELEASE(g_pNormalMapPS);

    SAFE_RELEASE(g_pSkyboxInputLayout);
    SAFE_RELEASE(g_pSkyboxVS);
    SAFE_RELEASE(g_pSkyboxPS);

    SAFE_RELEASE(g_pIndexBuffer);
    SAFE_RELEASE(g_pVertexBuffer);

    SAFE_RELEASE(g_pSkyboxIndexBuffer);
    SAFE_RELEASE(g_pSkyboxVertexBuffer);

    SAFE_RELEASE(g_pBackBufferRTV);
    SAFE_RELEASE(g_pDepthStencilView);
    SAFE_RELEASE(g_pSwapChain);
    SAFE_RELEASE(g_pTextureView);
    SAFE_RELEASE(g_pCubemapView);
    SAFE_RELEASE(g_pSampler);

#ifdef _DEBUG
    if (g_pDevice)
    {
        ID3D11Debug* pDebug = nullptr;
        if (SUCCEEDED(g_pDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug)))
        {
            pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            pDebug->Release();
        }
    }
#endif

    SAFE_RELEASE(g_pContext);
    SAFE_RELEASE(g_pDevice);
}

bool Initialize(HWND hWnd, UINT width, UINT height)
{
    g_hWnd = hWnd;
    g_width = width;
    g_height = height;
    g_lastFrameTime = (double)GetTickCount64() / 1000.0;

    if (!CreateDeviceAndSwapChain()) return false;
    if (!CreateRenderTargetAndDepthStencil()) return false;
    if (!CreateBuffers()) return false;
    if (!CompileShaders()) return false;
    if (!LoadTextures()) return false;

    SetupCubes();
    SetupPointLights();

    return true;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            UINT newW = LOWORD(lParam);
            UINT newH = HIWORD(lParam);
            if (newW > 0 && newH > 0)
                Resize(newW, newH);
        }
        return 0;

    case WM_KEYDOWN:
        HandleKey((UINT)wParam, true);
        return 0;

    case WM_KEYUP:
        HandleKey((UINT)wParam, false);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE,
    _In_ LPWSTR, _In_ int nCmdShow)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"D3D11LabClass";

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    RECT rc = { 0, 0, (LONG)WINDOW_WIDTH, (LONG)WINDOW_HEIGHT };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    HWND hWnd = CreateWindowW(wc.lpszClassName, L"2 Cubes with Dynamic Point Lights - Normal Mapping Demo",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        winWidth, winHeight, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    if (!Initialize(hWnd, WINDOW_WIDTH, WINDOW_HEIGHT))
    {
        Cleanup();
        DestroyWindow(hWnd);
        MessageBoxW(nullptr, L"Failed to initialize renderer", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

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
            Render();
    }

    Cleanup();
    return (int)msg.wParam;
}