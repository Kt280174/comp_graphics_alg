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
#include <ctime>
#include <cstdlib>

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

// Constants
const UINT MAX_OBJECTS = 10;
const UINT MATERIAL_COUNT = 2;
const std::wstring MATERIAL_PATHS[] = {
    L"Brick.dds",
    L"Kitty.dds"
};

// Forward declarations
struct MeshData;
struct ImageData;
struct VertexLayout;
struct InstanceData;

// Global variables
HWND g_MainWindow = nullptr;
ID3D11Device* g_Device = nullptr;
ID3D11DeviceContext* g_Context = nullptr;
IDXGISwapChain* g_SwapChain = nullptr;
ID3D11RenderTargetView* g_MainRTV = nullptr;
ID3D11DepthStencilView* g_DSV = nullptr;
ID3D11RasterizerState* g_NoCullRS = nullptr;
ID3D11ShaderResourceView* g_NormalSRV = nullptr;

// Buffers
ID3D11Buffer* g_VertexBuffer = nullptr;
ID3D11Buffer* g_IndexBuffer = nullptr;
ID3D11Buffer* g_SkyboxVB = nullptr;
ID3D11Buffer* g_SkyboxIB = nullptr;

// Shaders - Main
ID3D11VertexShader* g_MainVS = nullptr;
ID3D11PixelShader* g_MainPS = nullptr;
ID3D11InputLayout* g_MainLayout = nullptr;

// Shaders - Skybox
ID3D11VertexShader* g_SkyboxVS = nullptr;
ID3D11PixelShader* g_SkyboxPS = nullptr;
ID3D11InputLayout* g_SkyboxLayout = nullptr;

// Shaders - Instanced
ID3D11VertexShader* g_InstancedVS = nullptr;
ID3D11PixelShader* g_InstancedPS = nullptr;
ID3D11InputLayout* g_InstancedLayout = nullptr;

// Shaders - Post process
ID3D11VertexShader* g_PostVS = nullptr;
ID3D11PixelShader* g_PostPS = nullptr;

// Constant buffers
struct TransformData { XMMATRIX world; };
struct ViewProjectionData { XMMATRIX vp; };
struct FrameData {
    XMMATRIX vp;
    XMFLOAT4 camPosition;
    XMFLOAT4 lightCount;
    struct LightSource {
        XMFLOAT4 position;
        XMFLOAT4 color;
    } lights[10];
    XMFLOAT4 ambient;
};

struct PerInstance {
    XMMATRIX world;
    XMMATRIX worldInvTrans;
    XMFLOAT4 materialProps;
    XMFLOAT4 rotation;
};

ID3D11Buffer* g_FrameCB = nullptr;
ID3D11Buffer* g_TransformCB = nullptr;
ID3D11Buffer* g_ViewProjCB = nullptr;
ID3D11Buffer* g_InstanceCB = nullptr;
ID3D11Buffer* g_VisibleListCB = nullptr;

// Textures
ID3D11ShaderResourceView* g_MainTexture = nullptr;
ID3D11ShaderResourceView* g_Cubemap = nullptr;
ID3D11ShaderResourceView* g_TextureArray = nullptr;
ID3D11SamplerState* g_Sampler = nullptr;

// Render targets
ID3D11Texture2D* g_OffscreenBuffer = nullptr;
ID3D11RenderTargetView* g_OffscreenRTV = nullptr;
ID3D11ShaderResourceView* g_OffscreenSRV = nullptr;

// State objects
ID3D11BlendState* g_BlendState = nullptr;
ID3D11DepthStencilState* g_DepthReadOnly = nullptr;
ID3D11RasterizerState* g_BackfaceCullRS = nullptr;

// Instance data
PerInstance g_Instances[MAX_OBJECTS];
UINT g_InstanceCount = 0;
XMVECTOR g_LocalMin = XMVectorSet(-0.5f, -0.5f, -0.5f, 0.0f);
XMVECTOR g_LocalMax = XMVectorSet(0.5f, 0.5f, 0.5f, 0.0f);

// Camera and window
UINT g_ScreenWidth = 1280;
UINT g_ScreenHeight = 720;
float g_CameraYaw = 0.0f;
float g_CameraPitch = 0.0f;
float g_CameraDist = 3.0f;
bool g_InputLeft = false, g_InputRight = false, g_InputUp = false, g_InputDown = false;
double g_TimePrev = 0.0;
bool g_EnableFilter = true;

// Utility macros
#define RELEASE(p) if (p) { (p)->Release(); (p) = nullptr; }

// Helper functions
std::wstring GetAppPath()
{
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    std::wstring path(buffer);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        path = path.substr(0, pos + 1);
    return path;
}

// DDS structures
struct DDS_PIXELFORMAT
{
    DWORD size;
    DWORD flags;
    DWORD fourCC;
    DWORD rgbBits;
    DWORD rMask;
    DWORD gMask;
    DWORD bMask;
    DWORD aMask;
};

struct DDS_HEADER
{
    DWORD size;
    DWORD flags;
    DWORD height;
    DWORD width;
    DWORD pitchLinear;
    DWORD depth;
    DWORD mipCount;
    DWORD reserved1[11];
    DDS_PIXELFORMAT pixelFormat;
    DWORD caps;
    DWORD caps2;
    DWORD reserved2[3];
};

#define DDS_MAGIC_VALUE 0x20534444
#define FOURCC_DXT1_CODE MAKEFOURCC('D','X','T','1')
#define FOURCC_DXT3_CODE MAKEFOURCC('D','X','T','3')
#define FOURCC_DXT5_CODE MAKEFOURCC('D','X','T','5')

inline UINT AlignDiv(UINT a, UINT b) { return (a + b - 1) / b; }

UINT GetBlockSize(DXGI_FORMAT format)
{
    switch (format)
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

struct ImageInfo
{
    UINT32 rowPitch = 0;
    UINT32 levelCount = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT32 width = 0;
    UINT32 height = 0;
    void* data = nullptr;
};

bool ReadDDSFile(const wchar_t* filename, ImageInfo& info)
{
    HANDLE hFile = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD magic, bytesRead;
    ReadFile(hFile, &magic, sizeof(DWORD), &bytesRead, NULL);
    if (magic != DDS_MAGIC_VALUE)
    {
        CloseHandle(hFile);
        return false;
    }

    DDS_HEADER header;
    ReadFile(hFile, &header, sizeof(DDS_HEADER), &bytesRead, NULL);

    info.width = header.width;
    info.height = header.height;
    info.levelCount = (header.caps & 0x00400000) ? header.mipCount : 1;

    if (header.pixelFormat.flags & 0x00000004)
    {
        switch (header.pixelFormat.fourCC)
        {
        case FOURCC_DXT1_CODE:
            info.format = DXGI_FORMAT_BC1_UNORM;
            break;
        case FOURCC_DXT3_CODE:
            info.format = DXGI_FORMAT_BC2_UNORM;
            break;
        case FOURCC_DXT5_CODE:
            info.format = DXGI_FORMAT_BC3_UNORM;
            break;
        default:
            info.format = DXGI_FORMAT_UNKNOWN;
            break;
        }
    }
    else if (header.pixelFormat.flags & 0x00000040)
    {
        info.format = DXGI_FORMAT_UNKNOWN;
    }

    if (info.format == DXGI_FORMAT_UNKNOWN)
    {
        CloseHandle(hFile);
        return false;
    }

    UINT totalSize = 0;
    UINT w = info.width;
    UINT h = info.height;
    for (UINT i = 0; i < info.levelCount; ++i)
    {
        UINT bw = AlignDiv(w, 4u);
        UINT bh = AlignDiv(h, 4u);
        totalSize += bw * bh * GetBlockSize(info.format);
        w = max(1u, w / 2);
        h = max(1u, h / 2);
    }

    info.data = malloc(totalSize);
    if (!info.data)
    {
        CloseHandle(hFile);
        return false;
    }

    ReadFile(hFile, info.data, totalSize, &bytesRead, NULL);
    CloseHandle(hFile);
    return true;
}

// Core geometry
struct VertexPacked
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT3 tangent;
    XMFLOAT2 texcoord;
};

struct VertexSimple
{
    XMFLOAT3 position;
    XMFLOAT2 texcoord;
};

void BuildGeometry()
{
    const VertexPacked cubeVerts[] = {
        // Back face
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0,0,-1), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        // Front face
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0,0, 1), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        // Left face
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,1) },
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,1) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(-1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,0) },
        // Right face
        { XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(1,0) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(1,0,0), XMFLOAT3(0,0,1), XMFLOAT2(0,0) },
        // Top face
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,1) },
        { XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,1) },
        { XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(1,0) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0,1,0), XMFLOAT3(1,0,0), XMFLOAT2(0,0) },
        // Bottom face
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
    desc.ByteWidth = sizeof(cubeVerts);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = { cubeVerts };
    g_Device->CreateBuffer(&desc, &data, &g_VertexBuffer);

    desc.ByteWidth = sizeof(cubeIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = cubeIndices;
    g_Device->CreateBuffer(&desc, &data, &g_IndexBuffer);

    const VertexSimple skyVerts[] = {
        { XMFLOAT3(-10, -10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(10, -10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(10,  10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(-10,  10, -10), XMFLOAT2(0,0) },
        { XMFLOAT3(-10, -10,  10), XMFLOAT2(0,0) },
        { XMFLOAT3(10, -10,  10), XMFLOAT2(0,0) },
        { XMFLOAT3(10,  10,  10), XMFLOAT2(0,0) },
        { XMFLOAT3(-10,  10,  10), XMFLOAT2(0,0) }
    };

    const USHORT skyIndices[] = {
        0,2,1, 0,3,2,  4,5,6, 4,6,7,  0,7,3, 0,4,7,
        1,2,6, 1,6,5,  3,7,6, 3,6,2,  0,1,5, 0,5,4
    };

    desc.ByteWidth = sizeof(skyVerts);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem = skyVerts;
    g_Device->CreateBuffer(&desc, &data, &g_SkyboxVB);

    desc.ByteWidth = sizeof(skyIndices);
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = skyIndices;
    g_Device->CreateBuffer(&desc, &data, &g_SkyboxIB);
}

// Shader compilation
void SetupShaders()
{
    const char* mainVS = R"(
        cbuffer WorldCB : register(b0) { float4x4 world; }
        cbuffer ViewProjCB : register(b1) { float4x4 vp; }
        struct VS_IN {
            float3 pos : POSITION;
            float3 norm : NORMAL;
            float3 tang : TANGENT;
            float2 uv : TEXCOORD;
        };
        struct VS_OUT {
            float4 pos : SV_Position;
            float3 wPos : TEXCOORD0;
            float3 wNorm : NORMAL;
            float3 wTang : TANGENT;
            float2 uv : TEXCOORD1;
        };
        VS_OUT vs_main(VS_IN input) {
            VS_OUT output;
            float4 worldPos = mul(float4(input.pos, 1.0), world);
            output.pos = mul(worldPos, vp);
            output.wPos = worldPos.xyz;
            float3x3 normalMat = (float3x3)world;
            output.wNorm = normalize(mul(input.norm, normalMat));
            output.wTang = normalize(mul(input.tang, normalMat));
            output.uv = input.uv;
            return output;
        }
    )";

    const char* mainPS = R"(
        Texture2D colorTex : register(t0);
        Texture2D normalTex : register(t1);
        SamplerState samplerState : register(s0);
        cbuffer FrameCB : register(b2) {
            float4x4 vp;
            float4 camPos;
            float4 lightCount;
            struct Light {
                float4 pos;
                float4 color;
            } lights[10];
            float4 ambient;
        };
        struct VS_OUT {
            float4 pos : SV_Position;
            float3 wPos : TEXCOORD0;
            float3 wNorm : NORMAL;
            float3 wTang : TANGENT;
            float2 uv : TEXCOORD1;
        };
        float4 ps_main(VS_OUT input) : SV_Target0 {
            float4 texColor = colorTex.Sample(samplerState, input.uv);
            float3 tangentNorm = normalTex.Sample(samplerState, input.uv).xyz * 2.0 - 1.0;
            float3 N = normalize(input.wNorm);
            float3 T = normalize(input.wTang);
            float3 B = cross(N, T);
            float3x3 TBN = float3x3(T, B, N);
            float3 worldNormal = normalize(mul(tangentNorm, TBN));
            float3 result = ambient.xyz * texColor.xyz;
            for (int i = 0; i < lightCount.x; ++i) {
                float3 L = lights[i].pos.xyz - input.wPos.xyz;
                float dist = length(L);
                L = L / dist;
                float atten = 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);
                float diff = max(dot(worldNormal, L), 0.0);
                result += texColor.xyz * diff * atten * lights[i].color.xyz;
                float3 V = normalize(camPos.xyz - input.wPos.xyz);
                float3 R = reflect(-L, worldNormal);
                float spec = pow(max(dot(V, R), 0.0), 32.0);
                result += spec * atten * lights[i].color.xyz;
            }
            return float4(result, 1.0);
        }
    )";

    const char* skyVS = R"(
        cbuffer ViewProjCB : register(b1) { float4x4 vp; }
        struct VS_IN { float3 pos : POSITION; float2 uv : TEXCOORD; };
        struct VS_OUT { float4 pos : SV_Position; float3 texCoord : TEXCOORD; };
        VS_OUT vs_main(VS_IN input) {
            VS_OUT output;
            output.pos = mul(float4(input.pos, 1.0), vp);
            output.texCoord = input.pos;
            return output;
        }
    )";

    const char* skyPS = R"(
        TextureCube skyTex : register(t1);
        SamplerState skySampler : register(s1);
        struct VS_OUT { float4 pos : SV_Position; float3 texCoord : TEXCOORD; };
        float4 ps_main(VS_OUT input) : SV_Target0 {
            return skyTex.Sample(skySampler, input.texCoord);
        }
    )";

    const char* instancedVS = R"(
        cbuffer InstanceData : register(b1)
        {
            struct InstanceProps
            {
                float4x4 world;
                float4x4 worldInvTrans;
                float4 material;
                float4 rotation;
            } instances[100];
        };
        cbuffer ViewProjCB : register(b2)
        {
            float4x4 vp;
        };
        cbuffer VisibleList : register(b3) 
        {
            uint4 indices[100];
        };
        struct VS_IN
        {
            float3 pos    : POSITION;
            float3 tang   : TANGENT;
            float3 norm   : NORMAL;
            float2 uv     : TEXCOORD;
            uint instanceId : SV_InstanceID;
        };
        struct VS_OUT
        {
            float4 pos       : SV_Position;
            float4 worldPos  : POSITION;
            float3 tang      : TANGENT;
            float3 norm      : NORMAL;
            float2 uv        : TEXCOORD;
            nointerpolation uint id : INST_ID;
        };
        VS_OUT vs_main(VS_IN input)
        {
            VS_OUT output;
            uint globalId = indices[input.instanceId].x;   
            float4 worldPos = mul(instances[globalId].world, float4(input.pos, 1.0));
            output.pos = mul(worldPos, vp);
            output.worldPos = worldPos;
            output.uv = input.uv;
            output.tang = mul(instances[globalId].worldInvTrans, float4(input.tang, 0)).xyz;
            output.norm = mul(instances[globalId].worldInvTrans, float4(input.norm, 0)).xyz;
            output.id = input.instanceId; 
            return output;
        }
    )";

    const char* instancedPS = R"(
        Texture2DArray colorArray : register(t0);
        Texture2D normalMap : register(t1);
        SamplerState samplerState : register(s0);
        cbuffer InstanceData : register(b1)
        {
            struct InstanceProps
            {
                float4x4 world;
                float4x4 worldInvTrans;
                float4 material;
                float4 rotation;
            } instances[100];
        };
        cbuffer FrameCB : register(b3)
        {
            float4x4 vp;
            float4 camPos;
            float4 lightCount;
            struct Light
            {
                float4 pos;
                float4 color;
            } lights[10];
            float4 ambient;
        };
        cbuffer VisibleList : register(b4)
        {
            uint4 indices[100];
        };
        struct VS_OUT
        {
            float4 pos       : SV_Position;
            float4 worldPos  : POSITION;
            float3 tang      : TANGENT;
            float3 norm      : NORMAL;
            float2 uv        : TEXCOORD;
            nointerpolation uint id : INST_ID;
        };
        float4 ps_main(VS_OUT input) : SV_Target0
        {
            uint idx = indices[input.id].x;
            uint texId = (uint)instances[idx].material.z;
            float3 color = colorArray.Sample(samplerState, float3(input.uv, texId)).xyz;
            uint flags = asuint(instances[idx].material.w);
            float3 normal;
            if (flags == 1 && lightCount.y > 0)
            {
                float3 tangentNormal = normalMap.Sample(samplerState, input.uv).xyz * 2.0 - 1.0;
                float3 N = normalize(input.norm);
                float3 T = normalize(input.tang);
                float3 B = cross(N, T);
                normal = normalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * N);
            }
            else
            {
                normal = normalize(input.norm);
            }
            float shininess = instances[idx].material.x;
            float3 result = ambient.xyz * color;
            for (int i = 0; i < lightCount.x; ++i)
            {
                float3 L = lights[i].pos.xyz - input.worldPos.xyz;
                float dist = length(L);
                L = L / dist;
                float atten = 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);
                float diff = max(dot(normal, L), 0.0);
                result += color * diff * atten * lights[i].color.xyz;
                float3 V = normalize(camPos.xyz - input.worldPos.xyz);
                float3 R = reflect(-L, normal);
                float spec = pow(max(dot(V, R), 0.0), shininess);
                result += spec * atten * lights[i].color.xyz;
            }
            return float4(result, 1.0);
        }
    )";

    const char* postVS = R"(
        struct VS_IN { uint id : SV_VertexID; };
        struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD; };
        VS_OUT vs_main(VS_IN input) {
            VS_OUT output;
            float4 pos = float4(0,0,0,0);
            switch (input.id) {
                case 0: pos = float4(-1, 1, 0, 1); break;
                case 1: pos = float4(3,  1, 0, 1); break;
                case 2: pos = float4(-1, -3, 0, 1); break;
            }
            output.pos = pos;
            output.uv = float2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);
            return output;
        }
    )";

    const char* postPS = R"(
        Texture2D sceneTex : register(t0);
        SamplerState samplerState : register(s0);
        struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD; };
        float4 ps_main(VS_OUT input) : SV_Target0 {
            float3 color = sceneTex.Sample(samplerState, input.uv).rgb;
            float luminance = dot(color, float3(0.299, 0.587, 0.114));
            return float4(luminance, luminance, luminance, 1.0);
        }
    )";

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* vsBlob = nullptr, * psBlob = nullptr, * errorBlob = nullptr;

    // Main shaders
    D3DCompile(mainVS, strlen(mainVS), nullptr, nullptr, nullptr, "vs_main", "vs_5_0", flags, 0, &vsBlob, &errorBlob);
    if (errorBlob) { OutputDebugStringA((const char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
    g_Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_MainVS);

    D3DCompile(mainPS, strlen(mainPS), nullptr, nullptr, nullptr, "ps_main", "ps_5_0", flags, 0, &psBlob, &errorBlob);
    if (errorBlob) { OutputDebugStringA((const char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
    g_Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_MainPS);
    RELEASE(psBlob);

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_Device->CreateInputLayout(layoutDesc, 4, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_MainLayout);
    RELEASE(vsBlob);

    // Instanced shaders
    D3DCompile(instancedVS, strlen(instancedVS), nullptr, nullptr, nullptr, "vs_main", "vs_5_0", flags, 0, &vsBlob, &errorBlob);
    if (errorBlob) { OutputDebugStringA((const char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
    g_Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_InstancedVS);

    D3DCompile(instancedPS, strlen(instancedPS), nullptr, nullptr, nullptr, "ps_main", "ps_5_0", flags, 0, &psBlob, &errorBlob);
    if (errorBlob) { OutputDebugStringA((const char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
    g_Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_InstancedPS);
    RELEASE(psBlob);

    D3D11_INPUT_ELEMENT_DESC instLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_Device->CreateInputLayout(instLayout, 4, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_InstancedLayout);
    RELEASE(vsBlob);

    // Skybox shaders
    D3DCompile(skyVS, strlen(skyVS), nullptr, nullptr, nullptr, "vs_main", "vs_5_0", flags, 0, &vsBlob, &errorBlob);
    if (errorBlob) { OutputDebugStringA((const char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
    g_Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_SkyboxVS);

    D3DCompile(skyPS, strlen(skyPS), nullptr, nullptr, nullptr, "ps_main", "ps_5_0", flags, 0, &psBlob, &errorBlob);
    if (errorBlob) { OutputDebugStringA((const char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
    g_Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_SkyboxPS);

    D3D11_INPUT_ELEMENT_DESC skyLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    g_Device->CreateInputLayout(skyLayout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_SkyboxLayout);
    RELEASE(vsBlob);
    RELEASE(psBlob);

    // Post-process shaders
    ID3DBlob* postVSBlob = nullptr;
    ID3DBlob* postPSBlob = nullptr;
    D3DCompile(postVS, strlen(postVS), nullptr, nullptr, nullptr, "vs_main", "vs_5_0", flags, 0, &postVSBlob, &errorBlob);
    if (errorBlob) { OutputDebugStringA((const char*)errorBlob->GetBufferPointer()); RELEASE(errorBlob); }
    else { g_Device->CreateVertexShader(postVSBlob->GetBufferPointer(), postVSBlob->GetBufferSize(), nullptr, &g_PostVS); RELEASE(postVSBlob); }

    D3DCompile(postPS, strlen(postPS), nullptr, nullptr, nullptr, "ps_main", "ps_5_0", flags, 0, &postPSBlob, &errorBlob);
    if (errorBlob) { OutputDebugStringA((const char*)errorBlob->GetBufferPointer()); RELEASE(errorBlob); }
    else { g_Device->CreatePixelShader(postPSBlob->GetBufferPointer(), postPSBlob->GetBufferSize(), nullptr, &g_PostPS); RELEASE(postPSBlob); }
}

// Texture loading
void LoadMaterials()
{
    HRESULT hr;

    ImageInfo texInfo;
    std::wstring fullPath = GetAppPath() + L"..\\..\\texture\\Brick.dds";
    if (!ReadDDSFile(fullPath.c_str(), texInfo))
    {
        MessageBoxA(NULL, "Failed to load Brick.dds", "Error", MB_OK);
        return;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = texInfo.width;
    texDesc.Height = texInfo.height;
    texDesc.MipLevels = texInfo.levelCount;
    texDesc.ArraySize = 1;
    texDesc.Format = texInfo.format;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<D3D11_SUBRESOURCE_DATA> texData(texInfo.levelCount);

    UINT blockW = 0;
    UINT blockH = 0;
    UINT rowPitch = 0;

    UINT curWidth = texInfo.width;
    UINT curHeight = texInfo.height;
    UINT offset = 0;

    for (UINT mip = 0; mip < texInfo.levelCount; ++mip)
    {
        blockW = AlignDiv(curWidth, 4u);
        blockH = AlignDiv(curHeight, 4u);
        rowPitch = blockW * GetBlockSize(texInfo.format);

        texData[mip].pSysMem = (BYTE*)texInfo.data + offset;
        texData[mip].SysMemPitch = rowPitch;
        texData[mip].SysMemSlicePitch = 0;

        offset += rowPitch * blockH;

        curWidth = max(1u, curWidth / 2);
        curHeight = max(1u, curHeight / 2);
    }

    ID3D11Texture2D* pTexture = nullptr;
    hr = g_Device->CreateTexture2D(&texDesc, texData.data(), &pTexture);
    free(texInfo.data);
    if (FAILED(hr))
        return;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texInfo.format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = texInfo.levelCount;
    hr = g_Device->CreateShaderResourceView(pTexture, &srvDesc, &g_MainTexture);
    pTexture->Release();
    if (FAILED(hr))
        return;

    std::wstring normalPath = GetAppPath() + L"..\\..\\texture\\BrickNM.dds";
    ImageInfo normalInfo;
    if (ReadDDSFile(normalPath.c_str(), normalInfo))
    {
        D3D11_TEXTURE2D_DESC normDesc = {};
        normDesc.Width = normalInfo.width;
        normDesc.Height = normalInfo.height;
        normDesc.MipLevels = normalInfo.levelCount;
        normDesc.ArraySize = 1;
        normDesc.Format = normalInfo.format;
        normDesc.SampleDesc.Count = 1;
        normDesc.SampleDesc.Quality = 0;
        normDesc.Usage = D3D11_USAGE_IMMUTABLE;
        normDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        std::vector<D3D11_SUBRESOURCE_DATA> normData(normalInfo.levelCount);

        UINT normCurWidth = normalInfo.width;
        UINT normCurHeight = normalInfo.height;
        UINT normOffset = 0;

        for (UINT mip = 0; mip < normalInfo.levelCount; ++mip)
        {
            UINT normBlockW = AlignDiv(normCurWidth, 4u);
            UINT normBlockH = AlignDiv(normCurHeight, 4u);
            UINT normPitch = normBlockW * GetBlockSize(normalInfo.format);

            normData[mip].pSysMem = (BYTE*)normalInfo.data + normOffset;
            normData[mip].SysMemPitch = normPitch;
            normData[mip].SysMemSlicePitch = 0;

            normOffset += normPitch * normBlockH;

            normCurWidth = max(1u, normCurWidth / 2);
            normCurHeight = max(1u, normCurHeight / 2);
        }

        ID3D11Texture2D* pNormalTex = nullptr;
        hr = g_Device->CreateTexture2D(&normDesc, normData.data(), &pNormalTex);
        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC normSRV = {};
            normSRV.Format = normalInfo.format;
            normSRV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            normSRV.Texture2D.MostDetailedMip = 0;
            normSRV.Texture2D.MipLevels = normalInfo.levelCount;

            hr = g_Device->CreateShaderResourceView(pNormalTex, &normSRV, &g_NormalSRV);
            pNormalTex->Release();
        }

        free(normalInfo.data);
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
    g_Device->CreateSamplerState(&sampDesc, &g_Sampler);

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.FrontCounterClockwise = FALSE;
    g_Device->CreateRasterizerState(&rsDesc, &g_BackfaceCullRS);

    // Cubemap loading
    std::wstring skyPath = GetAppPath() + L"..\\..\\texture\\skybox\\";
    std::wstring faces[6] = {
        skyPath + L"posx.dds", skyPath + L"negx.dds",
        skyPath + L"posy.dds", skyPath + L"negy.dds",
        skyPath + L"posz.dds", skyPath + L"negz.dds"
    };

    ImageInfo faceInfo[6];
    bool success = true;
    for (int i = 0; i < 6; ++i)
    {
        if (!ReadDDSFile(faces[i].c_str(), faceInfo[i]))
        {
            success = false;
            break;
        }
    }

    if (success)
    {
        for (int i = 1; i < 6; ++i)
        {
            if (faceInfo[i].format != faceInfo[0].format ||
                faceInfo[i].width != faceInfo[0].width ||
                faceInfo[i].height != faceInfo[0].height)
            {
                success = false;
                break;
            }
        }
    }

    if (!success)
    {
        MessageBoxA(NULL, "Failed to load cubemap", "Error", MB_OK);
        return;
    }

    D3D11_TEXTURE2D_DESC cubeDesc = {};
    cubeDesc.Width = faceInfo[0].width;
    cubeDesc.Height = faceInfo[0].height;
    cubeDesc.MipLevels = 1;
    cubeDesc.ArraySize = 6;
    cubeDesc.Format = faceInfo[0].format;
    cubeDesc.SampleDesc.Count = 1;
    cubeDesc.SampleDesc.Quality = 0;
    cubeDesc.Usage = D3D11_USAGE_IMMUTABLE;
    cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    blockW = AlignDiv(cubeDesc.Width, 4u);
    blockH = AlignDiv(cubeDesc.Height, 4u);
    rowPitch = blockW * GetBlockSize(cubeDesc.Format);

    D3D11_SUBRESOURCE_DATA cubeData[6] = {};
    for (int i = 0; i < 6; ++i)
    {
        cubeData[i].pSysMem = faceInfo[i].data;
        cubeData[i].SysMemPitch = rowPitch;
        cubeData[i].SysMemSlicePitch = 0;
    }

    ID3D11Texture2D* pCubeTex = nullptr;
    hr = g_Device->CreateTexture2D(&cubeDesc, cubeData, &pCubeTex);

    for (int i = 0; i < 6; ++i)
        free(faceInfo[i].data);

    if (SUCCEEDED(hr))
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC cubeSRV = {};
        cubeSRV.Format = cubeDesc.Format;
        cubeSRV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        cubeSRV.TextureCube.MostDetailedMip = 0;
        cubeSRV.TextureCube.MipLevels = 1;

        g_Device->CreateShaderResourceView(pCubeTex, &cubeSRV, &g_Cubemap);
        pCubeTex->Release();
    }

    D3D11_RASTERIZER_DESC noCullDesc = {};
    noCullDesc.FillMode = D3D11_FILL_SOLID;
    noCullDesc.CullMode = D3D11_CULL_NONE;
    noCullDesc.FrontCounterClockwise = FALSE;
    g_Device->CreateRasterizerState(&noCullDesc, &g_NoCullRS);
}

void CreateTextureArray()
{
    std::vector<ImageInfo> textures(MATERIAL_COUNT);
    bool success = true;
    for (UINT i = 0; i < MATERIAL_COUNT; ++i)
    {
        std::wstring path = GetAppPath() + L"..\\..\\texture\\" + MATERIAL_PATHS[i];
        if (!ReadDDSFile(path.c_str(), textures[i]))
        {
            success = false;
            break;
        }
    }
    if (!success)
    {
        MessageBoxA(NULL, "Failed to load textures for array", "Error", MB_OK);
        return;
    }

    DXGI_FORMAT format = textures[0].format;
    UINT width = textures[0].width;
    UINT height = textures[0].height;
    UINT mipLevels = textures[0].levelCount;

    for (UINT i = 1; i < MATERIAL_COUNT; ++i)
    {
        if (textures[i].format != format || textures[i].width != width ||
            textures[i].height != height || textures[i].levelCount != mipLevels)
        {
            MessageBoxA(NULL, "Texture mismatch", "Error", MB_OK);
            return;
        }
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = mipLevels;
    texDesc.ArraySize = MATERIAL_COUNT;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    UINT blockW = AlignDiv(width, 4u);
    UINT blockH = AlignDiv(height, 4u);
    UINT pitch = blockW * GetBlockSize(format);

    std::vector<D3D11_SUBRESOURCE_DATA> initData;
    for (UINT i = 0; i < MATERIAL_COUNT; ++i)
    {
        UINT w = width;
        UINT h = height;
        UINT offset = 0;

        for (UINT mip = 0; mip < mipLevels; ++mip)
        {
            D3D11_SUBRESOURCE_DATA data = {};
            UINT bw = AlignDiv(w, 4u);
            UINT bh = AlignDiv(h, 4u);
            UINT mipPitch = bw * GetBlockSize(format);
            data.pSysMem = (BYTE*)textures[i].data + offset;
            data.SysMemPitch = mipPitch;
            initData.push_back(data);
            offset += mipPitch * bh;
            w = max(1u, w / 2);
            h = max(1u, h / 2);
        }
    }

    ID3D11Texture2D* pArrayTex = nullptr;
    HRESULT hr = g_Device->CreateTexture2D(&texDesc, initData.data(), &pArrayTex);
    for (auto& tex : textures) free(tex.data);
    if (FAILED(hr)) return;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MipLevels = mipLevels;
    srvDesc.Texture2DArray.ArraySize = MATERIAL_COUNT;
    hr = g_Device->CreateShaderResourceView(pArrayTex, &srvDesc, &g_TextureArray);
    pArrayTex->Release();
}

void InitInstances()
{
    g_InstanceCount = MAX_OBJECTS;
    float radius = 3.0f;
    srand((unsigned int)time(nullptr));

    for (UINT i = 0; i < MAX_OBJECTS; ++i)
    {
        float phi = XM_PI * (3.0f - sqrtf(5.0f));
        float y = 1.0f - (i / (float)(MAX_OBJECTS - 1)) * 2.0f;
        float r = sqrtf(1.0f - y * y);
        float theta = i * phi * 2.0f * XM_PI;

        float x = cosf(theta) * r;
        float z = sinf(theta) * r;

        XMFLOAT3 pos(x * radius, y * radius, z * radius);

        XMMATRIX world = XMMatrixTranslation(pos.x, pos.y, pos.z);
        XMMATRIX invTrans = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
        g_Instances[i].world = world;
        g_Instances[i].worldInvTrans = invTrans;

        int texId = rand() % MATERIAL_COUNT;
        float shininess = 32.0f;
        float speed = 0.5f + (rand() % 100) / 100.0f;
        float hasNormal = (texId == 2) ? 1.0f : 0.0f;
        g_Instances[i].materialProps = XMFLOAT4(shininess, speed, (float)texId, hasNormal);
        g_Instances[i].rotation = XMFLOAT4(pos.x, pos.y, pos.z, 0.0f);
    }
}

void UpdateTransforms(double time)
{
    for (UINT i = 0; i < g_InstanceCount; ++i)
    {
        float angle = (float)time * g_Instances[i].materialProps.y;
        XMMATRIX rot = XMMatrixRotationY(angle);
        XMMATRIX trans = XMMatrixTranslation(g_Instances[i].rotation.x, g_Instances[i].rotation.y, g_Instances[i].rotation.z);
        g_Instances[i].world = rot * trans;
        g_Instances[i].worldInvTrans = XMMatrixTranspose(XMMatrixInverse(nullptr, g_Instances[i].world));
    }
}

void BuildFrustum(const XMMATRIX& vp, XMVECTOR planes[6])
{
    XMVECTOR r0 = XMVectorSet(vp.r[0].m128_f32[0], vp.r[1].m128_f32[0], vp.r[2].m128_f32[0], vp.r[3].m128_f32[0]);
    XMVECTOR r1 = XMVectorSet(vp.r[0].m128_f32[1], vp.r[1].m128_f32[1], vp.r[2].m128_f32[1], vp.r[3].m128_f32[1]);
    XMVECTOR r2 = XMVectorSet(vp.r[0].m128_f32[2], vp.r[1].m128_f32[2], vp.r[2].m128_f32[2], vp.r[3].m128_f32[2]);
    XMVECTOR r3 = XMVectorSet(vp.r[0].m128_f32[3], vp.r[1].m128_f32[3], vp.r[2].m128_f32[3], vp.r[3].m128_f32[3]);

    planes[0] = r3 + r0;
    planes[1] = r3 - r0;
    planes[2] = r3 + r1;
    planes[3] = r3 - r1;
    planes[4] = r3 + r2;
    planes[5] = r3 - r2;

    for (int i = 0; i < 6; ++i)
        planes[i] = planes[i] / XMVector3Length(planes[i]);
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
        XMVECTOR wc = XMVector4Transform(corners[i], transform);
        worldMin = XMVectorMin(worldMin, wc);
        worldMax = XMVectorMax(worldMax, wc);
    }
}

bool TestFrustum(const XMVECTOR planes[6], const XMVECTOR& aabbMin, const XMVECTOR& aabbMax)
{
    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR p = aabbMin;
        if (XMVectorGetX(planes[i]) >= 0) p = XMVectorSetX(p, XMVectorGetX(aabbMax));
        if (XMVectorGetY(planes[i]) >= 0) p = XMVectorSetY(p, XMVectorGetY(aabbMax));
        if (XMVectorGetZ(planes[i]) >= 0) p = XMVectorSetZ(p, XMVectorGetZ(aabbMax));
        if (XMVector4Dot(p, planes[i]).m128_f32[0] < 0) return false;
    }
    return true;
}

// Direct3D initialization
bool InitD3D()
{
    HRESULT hr;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    DXGI_SWAP_CHAIN_DESC swapDesc = {};
    swapDesc.BufferCount = 2;
    swapDesc.BufferDesc.Width = g_ScreenWidth;
    swapDesc.BufferDesc.Height = g_ScreenHeight;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = g_MainWindow;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtained;

    hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        flags, levels, 1, D3D11_SDK_VERSION,
        &swapDesc, &g_SwapChain, &g_Device, &obtained, &g_Context
    );

    if (FAILED(hr))
        return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) return false;

    hr = g_Device->CreateRenderTargetView(pBackBuffer, nullptr, &g_MainRTV);
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

    ID3D11Texture2D* pDepth = nullptr;
    hr = g_Device->CreateTexture2D(&depthDesc, nullptr, &pDepth);
    if (FAILED(hr)) return false;

    hr = g_Device->CreateDepthStencilView(pDepth, nullptr, &g_DSV);
    pDepth->Release();
    if (FAILED(hr)) return false;

    return true;
}

void CreateOffscreenBuffer(UINT width, UINT height)
{
    RELEASE(g_OffscreenBuffer);
    RELEASE(g_OffscreenRTV);
    RELEASE(g_OffscreenSRV);

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

    HRESULT hr = g_Device->CreateTexture2D(&desc, nullptr, &g_OffscreenBuffer);
    if (SUCCEEDED(hr))
        hr = g_Device->CreateRenderTargetView(g_OffscreenBuffer, nullptr, &g_OffscreenRTV);
    if (SUCCEEDED(hr))
        hr = g_Device->CreateShaderResourceView(g_OffscreenBuffer, nullptr, &g_OffscreenSRV);
    assert(SUCCEEDED(hr));
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (g_SwapChain && wParam != SIZE_MINIMIZED)
        {
            UINT newW = LOWORD(lParam);
            UINT newH = HIWORD(lParam);
            if (newW > 0 && newH > 0)
            {
                g_Context->OMSetRenderTargets(0, nullptr, nullptr);
                RELEASE(g_MainRTV);
                RELEASE(g_DSV);

                g_SwapChain->ResizeBuffers(2, newW, newH, DXGI_FORMAT_UNKNOWN, 0);

                ID3D11Texture2D* pBB = nullptr;
                g_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBB);
                if (pBB)
                {
                    g_Device->CreateRenderTargetView(pBB, nullptr, &g_MainRTV);
                    pBB->Release();
                }

                D3D11_TEXTURE2D_DESC depthDesc = {};
                depthDesc.Width = newW;
                depthDesc.Height = newH;
                depthDesc.MipLevels = 1;
                depthDesc.ArraySize = 1;
                depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
                depthDesc.SampleDesc.Count = 1;
                depthDesc.SampleDesc.Quality = 0;
                depthDesc.Usage = D3D11_USAGE_DEFAULT;
                depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

                ID3D11Texture2D* pDepthTex = nullptr;
                g_Device->CreateTexture2D(&depthDesc, nullptr, &pDepthTex);
                if (pDepthTex)
                {
                    g_Device->CreateDepthStencilView(pDepthTex, nullptr, &g_DSV);
                    pDepthTex->Release();
                }

                g_ScreenWidth = newW;
                g_ScreenHeight = newH;
                CreateOffscreenBuffer(g_ScreenWidth, g_ScreenHeight);
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_LEFT)  g_InputLeft = true;
        if (wParam == VK_RIGHT) g_InputRight = true;
        if (wParam == VK_UP)    g_InputUp = true;
        if (wParam == VK_DOWN)  g_InputDown = true;
        if (wParam == 'F')      g_EnableFilter = !g_EnableFilter;
        return 0;

    case WM_KEYUP:
        if (wParam == VK_LEFT)  g_InputLeft = false;
        if (wParam == VK_RIGHT) g_InputRight = false;
        if (wParam == VK_UP)    g_InputUp = false;
        if (wParam == VK_DOWN)  g_InputDown = false;
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void UpdateCamera(double dt)
{
    float speed = 1.0f;
    if (g_InputLeft)  g_CameraYaw -= speed * (float)dt;
    if (g_InputRight) g_CameraYaw += speed * (float)dt;
    if (g_InputUp)    g_CameraPitch += speed * (float)dt;
    if (g_InputDown)  g_CameraPitch -= speed * (float)dt;

    const float maxPitch = 1.5f;
    g_CameraPitch = max(-maxPitch, min(maxPitch, g_CameraPitch));
}

void RenderFrame()
{
    if (!g_Context || !g_MainRTV || !g_SwapChain)
        return;

    double now = (double)GetTickCount64() / 1000.0;
    double delta = now - g_TimePrev;
    g_TimePrev = now;

    UpdateCamera(delta);

    g_Context->ClearState();

    ID3D11RenderTargetView* renderTarget = g_EnableFilter ? g_OffscreenRTV : g_MainRTV;
    g_Context->OMSetRenderTargets(1, &renderTarget, g_DSV);

    const float clearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    g_Context->ClearRenderTargetView(renderTarget, clearColor);
    g_Context->ClearDepthStencilView(g_DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT vp = { 0, 0, (float)g_ScreenWidth, (float)g_ScreenHeight, 0.0f, 1.0f };
    g_Context->RSSetViewports(1, &vp);
    g_Context->RSSetState(g_BackfaceCullRS);

    // Camera matrices
    float camX = g_CameraDist * sin(g_CameraYaw) * cos(g_CameraPitch);
    float camY = g_CameraDist * sin(g_CameraPitch);
    float camZ = g_CameraDist * cos(g_CameraYaw) * cos(g_CameraPitch);
    XMVECTOR eye = XMVectorSet(camX, camY, camZ, 0.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, target, up);
    float aspect = (float)g_ScreenWidth / (float)g_ScreenHeight;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3.0f, aspect, 0.1f, 100.0f);
    XMMATRIX viewProj = view * proj;

    // Skybox
    {
        XMMATRIX viewNoTrans = view;
        viewNoTrans.r[3] = XMVectorSet(0, 0, 0, 1);
        XMMATRIX vpSky = viewNoTrans * proj;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(g_Context->Map(g_ViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            ViewProjectionData* pData = (ViewProjectionData*)mapped.pData;
            XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(vpSky));
            g_Context->Unmap(g_ViewProjCB, 0);
        }

        D3D11_DEPTH_STENCIL_DESC dsSky = {};
        dsSky.DepthEnable = TRUE;
        dsSky.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsSky.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        ID3D11DepthStencilState* pDSSky = nullptr;
        g_Device->CreateDepthStencilState(&dsSky, &pDSSky);
        g_Context->OMSetDepthStencilState(pDSSky, 0);
        RELEASE(pDSSky);

        D3D11_RASTERIZER_DESC rsSky = {};
        rsSky.FillMode = D3D11_FILL_SOLID;
        rsSky.CullMode = D3D11_CULL_NONE;
        ID3D11RasterizerState* pRSSky = nullptr;
        g_Device->CreateRasterizerState(&rsSky, &pRSSky);
        g_Context->RSSetState(pRSSky);
        RELEASE(pRSSky);

        g_Context->VSSetShader(g_SkyboxVS, nullptr, 0);
        g_Context->PSSetShader(g_SkyboxPS, nullptr, 0);
        g_Context->IASetInputLayout(g_SkyboxLayout);
        UINT stride = sizeof(VertexSimple);
        UINT offset = 0;
        ID3D11Buffer* vbs[] = { g_SkyboxVB };
        g_Context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
        g_Context->IASetIndexBuffer(g_SkyboxIB, DXGI_FORMAT_R16_UINT, 0);
        g_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* cbs[] = { nullptr, g_ViewProjCB };
        g_Context->VSSetConstantBuffers(0, 2, cbs);
        ID3D11ShaderResourceView* skySRV[] = { g_Cubemap };
        g_Context->PSSetShaderResources(1, 1, skySRV);
        ID3D11SamplerState* samplers[] = { g_Sampler };
        g_Context->PSSetSamplers(1, 1, samplers);

        g_Context->DrawIndexed(36, 0, 0);
        g_Context->RSSetState(g_BackfaceCullRS);
        g_Context->OMSetDepthStencilState(nullptr, 0);
    }

    // Update constant buffers
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_Context->Map(g_ViewProjCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        ViewProjectionData* pData = (ViewProjectionData*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pData->vp, XMMatrixTranspose(viewProj));
        g_Context->Unmap(g_ViewProjCB, 0);
    }

    if (SUCCEEDED(g_Context->Map(g_FrameCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        FrameData* pFrame = (FrameData*)mapped.pData;
        XMStoreFloat4x4((XMFLOAT4X4*)&pFrame->vp, XMMatrixTranspose(viewProj));
        pFrame->camPosition = XMFLOAT4(camX, camY, camZ, 1.0f);
        pFrame->lightCount.x = 2;
        pFrame->lights[0].position = XMFLOAT4(0.0f, 2.0f, 10.0f, 1.0f);
        pFrame->lights[0].color = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
        pFrame->lights[1].position = XMFLOAT4(0.0f, 2.0f, 2.5f, 1.0f);
        pFrame->lights[1].color = XMFLOAT4(0.6f, 0.8f, 1.0f, 1.0f);
        pFrame->ambient = XMFLOAT4(0.8f, 0.8f, 0.f, 1.0f);
        g_Context->Unmap(g_FrameCB, 0);
    }

    UpdateTransforms(now);
    g_Context->UpdateSubresource(g_InstanceCB, 0, nullptr, g_Instances, sizeof(PerInstance) * MAX_OBJECTS, 0);

    // Frustum culling
    XMVECTOR frustumPlanes[6];
    BuildFrustum(viewProj, frustumPlanes);
    std::vector<UINT> visible;
    XMVECTOR localMin = XMVectorSet(-0.5f, -0.5f, -0.5f, 1.0f);
    XMVECTOR localMax = XMVectorSet(0.5f, 0.5f, 0.5f, 1.0f);
    for (UINT i = 0; i < g_InstanceCount; ++i)
    {
        XMVECTOR wMin, wMax;
        TransformBounds(g_Instances[i].world, localMin, localMax, wMin, wMax);
        if (TestFrustum(frustumPlanes, wMin, wMax))
            visible.push_back(i);
    }

    std::vector<XMUINT4> packed(MAX_OBJECTS);
    for (size_t j = 0; j < visible.size(); ++j)
        packed[j].x = visible[j];
    g_Context->UpdateSubresource(g_VisibleListCB, 0, nullptr, packed.data(), sizeof(XMUINT4) * MAX_OBJECTS, 0);

    // Draw instances
    UINT stride = sizeof(VertexPacked);
    UINT offset = 0;
    ID3D11Buffer* vbs[] = { g_VertexBuffer };
    g_Context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    g_Context->IASetIndexBuffer(g_IndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_Context->IASetInputLayout(g_InstancedLayout);
    g_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_Context->VSSetShader(g_InstancedVS, nullptr, 0);
    g_Context->PSSetShader(g_InstancedPS, nullptr, 0);

    ID3D11Buffer* vsCBs[] = { nullptr, g_InstanceCB, g_ViewProjCB, g_VisibleListCB };
    g_Context->VSSetConstantBuffers(0, 4, vsCBs);

    g_Context->PSSetConstantBuffers(1, 1, &g_InstanceCB);
    g_Context->PSSetConstantBuffers(3, 1, &g_FrameCB);
    g_Context->PSSetConstantBuffers(4, 1, &g_VisibleListCB);

    ID3D11ShaderResourceView* texArraySRV[] = { g_TextureArray, g_NormalSRV };
    g_Context->PSSetShaderResources(0, 2, texArraySRV);
    ID3D11SamplerState* samp = g_Sampler;
    g_Context->PSSetSamplers(0, 1, &samp);

    if (visible.size() > 0)
        g_Context->DrawIndexedInstanced(36, (UINT)visible.size(), 0, 0, 0);

    // Post-process filter
    if (g_EnableFilter)
    {
        g_Context->OMSetRenderTargets(1, &g_MainRTV, nullptr);
        g_Context->ClearRenderTargetView(g_MainRTV, clearColor);

        g_Context->OMSetDepthStencilState(nullptr, 0);
        g_Context->RSSetState(nullptr);
        g_Context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        g_Context->IASetInputLayout(nullptr);
        g_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        g_Context->VSSetShader(g_PostVS, nullptr, 0);
        g_Context->PSSetShader(g_PostPS, nullptr, 0);
        ID3D11ShaderResourceView* srv[] = { g_OffscreenSRV };
        g_Context->PSSetShaderResources(0, 1, srv);
        ID3D11SamplerState* sampler[] = { g_Sampler };
        g_Context->PSSetSamplers(0, 1, sampler);
        g_Context->Draw(3, 0);
    }

    g_SwapChain->Present(1, 0);
}

void Cleanup()
{
    if (g_Context)
        g_Context->ClearState();

    RELEASE(g_TransformCB);
    RELEASE(g_ViewProjCB);
    RELEASE(g_FrameCB);
    RELEASE(g_MainLayout);
    RELEASE(g_MainVS);
    RELEASE(g_MainPS);
    RELEASE(g_SkyboxLayout);
    RELEASE(g_SkyboxVS);
    RELEASE(g_SkyboxPS);
    RELEASE(g_IndexBuffer);
    RELEASE(g_VertexBuffer);
    RELEASE(g_SkyboxIB);
    RELEASE(g_SkyboxVB);
    RELEASE(g_MainRTV);
    RELEASE(g_DSV);
    RELEASE(g_SwapChain);
    RELEASE(g_MainTexture);
    RELEASE(g_Cubemap);
    RELEASE(g_Sampler);
    RELEASE(g_BackfaceCullRS);
    RELEASE(g_NoCullRS);
    RELEASE(g_NormalSRV);

    RELEASE(g_InstancedVS);
    RELEASE(g_InstancedPS);
    RELEASE(g_InstancedLayout);
    RELEASE(g_InstanceCB);
    RELEASE(g_VisibleListCB);
    RELEASE(g_TextureArray);

    RELEASE(g_OffscreenBuffer);
    RELEASE(g_OffscreenRTV);
    RELEASE(g_OffscreenSRV);
    RELEASE(g_PostVS);
    RELEASE(g_PostPS);

#ifdef _DEBUG
    if (g_Device)
    {
        ID3D11Debug* pDebug = nullptr;
        if (SUCCEEDED(g_Device->QueryInterface(__uuidof(ID3D11Debug), (void**)&pDebug)))
        {
            pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            pDebug->Release();
        }
    }
#endif

    RELEASE(g_Context);
    RELEASE(g_Device);
}

// Entry point
int WINAPI wWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"D3D11App";

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"Failed to register window", L"Error", MB_OK);
        return 0;
    }

    RECT rc = { 0, 0, (LONG)g_ScreenWidth, (LONG)g_ScreenHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    g_MainWindow = CreateWindowW(wc.lpszClassName, L"3D Rendering Demo",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        winW, winH, nullptr, nullptr, hInst, nullptr);
    if (!g_MainWindow)
    {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK);
        return 0;
    }

    ShowWindow(g_MainWindow, nCmdShow);
    UpdateWindow(g_MainWindow);

    if (!InitD3D())
    {
        Cleanup();
        DestroyWindow(g_MainWindow);
        return -1;
    }

    BuildGeometry();
    CreateOffscreenBuffer(g_ScreenWidth, g_ScreenHeight);
    SetupShaders();
    LoadMaterials();
    CreateTextureArray();
    InitInstances();

    // Create constant buffers
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(TransformData);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_Device->CreateBuffer(&desc, nullptr, &g_TransformCB);

    desc.ByteWidth = sizeof(ViewProjectionData);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_Device->CreateBuffer(&desc, nullptr, &g_ViewProjCB);

    desc.ByteWidth = sizeof(FrameData);
    g_Device->CreateBuffer(&desc, nullptr, &g_FrameCB);

    desc.ByteWidth = sizeof(PerInstance) * MAX_OBJECTS;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = 0;
    g_Device->CreateBuffer(&desc, nullptr, &g_InstanceCB);

    desc.ByteWidth = sizeof(XMUINT4) * MAX_OBJECTS;
    g_Device->CreateBuffer(&desc, nullptr, &g_VisibleListCB);

    g_TimePrev = (double)GetTickCount64() / 1000.0;

    MSG msg = {};
    bool running = true;
    while (running)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (running)
            RenderFrame();
    }

    Cleanup();
    return (int)msg.wParam;
}