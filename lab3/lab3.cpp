#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <chrono>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

using namespace DirectX;

UINT INITIAL_WIDTH = 1280;
UINT INITIAL_HEIGHT = 720;
float ROTATION_SPEED = 1.5f;
float CAMERA_DISTANCE = 4.5f;
float MAX_PITCH = 1.4f;

HINSTANCE g_hInstance = nullptr;
HWND g_hMainWindow = nullptr;


ID3D11Device* g_d3dDevice = nullptr;
ID3D11DeviceContext* g_d3dContext = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTarget = nullptr;

ID3D11VertexShader* g_vertexShader = nullptr;
ID3D11PixelShader* g_pixelShader = nullptr;
ID3D11InputLayout* g_inputLayout = nullptr;
ID3D11Buffer* g_vertexBuffer = nullptr;
ID3D11Buffer* g_indexBuffer = nullptr;
ID3D11Buffer* g_transformBuffer = nullptr;
ID3D11Buffer* g_cameraBuffer = nullptr;


UINT g_windowWidth = INITIAL_WIDTH;
UINT g_windowHeight = INITIAL_HEIGHT;


struct VertexFormat
{
    float position[3];
    unsigned char color[4];
};

struct TransformData
{
    XMMATRIX worldTransform;
};

struct CameraData
{
    XMMATRIX viewProjection;
};


struct CameraController
{
    float yaw = 0.0f;
    float pitch = 0.2f;
    float distance = CAMERA_DISTANCE;
    bool leftPressed = false;
    bool rightPressed = false;
    bool upPressed = false;
    bool downPressed = false;
} g_cameraCtrl;

std::chrono::high_resolution_clock::time_point g_lastFrameTime;

// Shader source
const char* g_shaderVertexCode = R"(
cbuffer WorldMatrix : register(b0) {
    float4x4 world;
}

cbuffer ViewProjectionMatrix : register(b1) {
    float4x4 viewProj;
}

struct VS_INPUT {
    float3 position : POSITION;
    float4 color : COLOR;
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    float4 worldPos = mul(float4(input.position, 1.0), world);
    output.position = mul(worldPos, viewProj);
    output.color = input.color;
    return output;
}
)";

const char* g_shaderPixelCode = R"(
struct PS_INPUT {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

float4 main(PS_INPUT input) : SV_TARGET {
    return input.color;
}
)";

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
bool InitializeD3D();
bool CreateGeometryBuffers();
bool CompileAndCreateShaders();
void RenderScene();
void CleanupResources();
void HandleResize(UINT width, UINT height);
void UpdateCamera(float deltaTime);


#define SAFE_RELEASE(ptr) if(ptr) { ptr->Release(); ptr = nullptr; }


int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    g_hInstance = hInstance;

    // Setup window class
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"D3D11TutorialClass";

    if (!RegisterClassExW(&windowClass))
    {
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_ICONERROR);
        return -1;
    }

    // Create the window
    RECT windowRect = { 0, 0, INITIAL_WIDTH, INITIAL_HEIGHT };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    g_hMainWindow = CreateWindowW(
        windowClass.lpszClassName,
        L"D3D11 - Rotating Cube",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_hMainWindow)
    {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_ICONERROR);
        return -1;
    }

    ShowWindow(g_hMainWindow, nCmdShow);

    // Initialize graphics
    if (!InitializeD3D())
    {
        MessageBoxW(nullptr, L"Failed to initialize Direct3D", L"Error", MB_ICONERROR);
        CleanupResources();
        return -1;
    }

    // Setup scene resources
    if (!CreateGeometryBuffers() || !CompileAndCreateShaders())
    {
        MessageBoxW(nullptr, L"Failed to create resources", L"Error", MB_ICONERROR);
        CleanupResources();
        return -1;
    }

    // Timing initialization
    g_lastFrameTime = std::chrono::high_resolution_clock::now();

    // Message loop
    MSG message = {};
    bool isRunning = true;

    while (isRunning)
    {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
                isRunning = false;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (isRunning)
            RenderScene();
    }

    CleanupResources();
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
    {
        UINT newWidth = LOWORD(lParam);
        UINT newHeight = HIWORD(lParam);
        if (g_swapChain && newWidth > 0 && newHeight > 0 && wParam != SIZE_MINIMIZED)
            HandleResize(newWidth, newHeight);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_LEFT) g_cameraCtrl.leftPressed = true;
        if (wParam == VK_RIGHT) g_cameraCtrl.rightPressed = true;
        if (wParam == VK_UP) g_cameraCtrl.upPressed = true;
        if (wParam == VK_DOWN) g_cameraCtrl.downPressed = true;
        return 0;

    case WM_KEYUP:
        if (wParam == VK_LEFT) g_cameraCtrl.leftPressed = false;
        if (wParam == VK_RIGHT) g_cameraCtrl.rightPressed = false;
        if (wParam == VK_UP) g_cameraCtrl.upPressed = false;
        if (wParam == VK_DOWN) g_cameraCtrl.downPressed = false;
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}


bool InitializeD3D()
{
    HRESULT result;

    UINT deviceFlags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#ifdef _DEBUG
    deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    DXGI_SWAP_CHAIN_DESC swapDesc = {};
    swapDesc.BufferCount = 2;
    swapDesc.BufferDesc.Width = INITIAL_WIDTH;
    swapDesc.BufferDesc.Height = INITIAL_HEIGHT;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = g_hMainWindow;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL selectedLevel;

    result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        deviceFlags, featureLevels, 1, D3D11_SDK_VERSION,
        &swapDesc, &g_swapChain, &g_d3dDevice, &selectedLevel, &g_d3dContext);

    if (FAILED(result))
    {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            deviceFlags, featureLevels, 1, D3D11_SDK_VERSION,
            &swapDesc, &g_swapChain, &g_d3dDevice, &selectedLevel, &g_d3dContext);

        if (FAILED(result))
            return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    result = g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (FAILED(result))
        return false;

    result = g_d3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
    backBuffer->Release();

    return SUCCEEDED(result);
}

bool CreateGeometryBuffers()
{
    VertexFormat cubeVertices[] = {
        // Front face (Z-)
        {{-0.5f, -0.5f, -0.5f}, {255, 0, 0, 255}},
        {{ 0.5f, -0.5f, -0.5f}, {0, 0, 255, 255}},
        {{ 0.5f,  0.5f, -0.5f}, {0, 255, 0, 255}},
        {{-0.5f,  0.5f, -0.5f}, {255, 255, 0, 255}},

        // Back face (Z+)
        {{-0.5f, -0.5f,  0.5f}, {255, 0, 255, 255}},
        {{ 0.5f, -0.5f,  0.5f}, {0, 255, 255, 255}},
        {{ 0.5f,  0.5f,  0.5f}, {255, 128, 0, 255}},
        {{-0.5f,  0.5f,  0.5f}, {128, 128, 128, 255}}
    };

    unsigned short cubeIndices[] = {
        0, 1, 2, 0, 2, 3,      // Front
        4, 6, 5, 4, 7, 6,      // Back
        0, 4, 5, 0, 5, 1,      // Bottom
        3, 2, 6, 3, 6, 7,      // Top
        0, 3, 7, 0, 7, 4,      // Left
        1, 5, 6, 1, 6, 2       // Right
    };

    D3D11_BUFFER_DESC bufferDesc = {};
    D3D11_SUBRESOURCE_DATA initData = {};

    bufferDesc.ByteWidth = sizeof(cubeVertices);
    bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    initData.pSysMem = cubeVertices;

    if (FAILED(g_d3dDevice->CreateBuffer(&bufferDesc, &initData, &g_vertexBuffer)))
        return false;

    // Index buffer
    bufferDesc.ByteWidth = sizeof(cubeIndices);
    bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    initData.pSysMem = cubeIndices;

    if (FAILED(g_d3dDevice->CreateBuffer(&bufferDesc, &initData, &g_indexBuffer)))
        return false;

    bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(TransformData);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    if (FAILED(g_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &g_transformBuffer)))
        return false;

    bufferDesc.ByteWidth = sizeof(CameraData);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    if (FAILED(g_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &g_cameraBuffer)))
        return false;

    return true;
}

// Compile shaders and create input layout
bool CompileAndCreateShaders()
{
    ID3DBlob* vertexCode = nullptr;
    ID3DBlob* pixelCode = nullptr;
    ID3DBlob* errorMsg = nullptr;

    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG;
#endif

    // Compile vertex shader
    HRESULT hr = D3DCompile(g_shaderVertexCode, strlen(g_shaderVertexCode), nullptr,
        nullptr, nullptr, "main", "vs_5_0", compileFlags, 0, &vertexCode, &errorMsg);

    if (FAILED(hr))
    {
        if (errorMsg)
        {
            OutputDebugStringA((char*)errorMsg->GetBufferPointer());
            errorMsg->Release();
        }
        return false;
    }

    hr = D3DCompile(g_shaderPixelCode, strlen(g_shaderPixelCode), nullptr,
        nullptr, nullptr, "main", "ps_5_0", compileFlags, 0, &pixelCode, &errorMsg);

    if (FAILED(hr))
    {
        if (errorMsg)
        {
            OutputDebugStringA((char*)errorMsg->GetBufferPointer());
            errorMsg->Release();
        }
        return false;
    }

    // Create shader objects
    if (FAILED(g_d3dDevice->CreateVertexShader(vertexCode->GetBufferPointer(),
        vertexCode->GetBufferSize(), nullptr, &g_vertexShader)))
        return false;

    if (FAILED(g_d3dDevice->CreatePixelShader(pixelCode->GetBufferPointer(),
        pixelCode->GetBufferSize(), nullptr, &g_pixelShader)))
        return false;

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    if (FAILED(g_d3dDevice->CreateInputLayout(layoutDesc, 2,
        vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), &g_inputLayout)))
        return false;

    SAFE_RELEASE(vertexCode);
    SAFE_RELEASE(pixelCode);

    return true;
}

// Update camera based on input
void UpdateCamera(float deltaTime)
{
    float moveSpeed = ROTATION_SPEED * deltaTime;

    if (g_cameraCtrl.leftPressed)
        g_cameraCtrl.yaw -= moveSpeed;
    if (g_cameraCtrl.rightPressed)
        g_cameraCtrl.yaw += moveSpeed;
    if (g_cameraCtrl.upPressed)
        g_cameraCtrl.pitch += moveSpeed;
    if (g_cameraCtrl.downPressed)
        g_cameraCtrl.pitch -= moveSpeed;

    if (g_cameraCtrl.pitch > MAX_PITCH)
        g_cameraCtrl.pitch = MAX_PITCH;
    if (g_cameraCtrl.pitch < -MAX_PITCH)
        g_cameraCtrl.pitch = -MAX_PITCH;
}

// Render a frame
void RenderScene()
{

    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - g_lastFrameTime).count();
    g_lastFrameTime = currentTime;

    UpdateCamera(deltaTime);
    g_d3dContext->OMSetRenderTargets(1, &g_renderTarget, nullptr);
    float bgColor[4] = { 0.25f, 0.25f, 0.3f, 1.0f };
    g_d3dContext->ClearRenderTargetView(g_renderTarget, bgColor);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(g_windowWidth);
    viewport.Height = static_cast<float>(g_windowHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    g_d3dContext->RSSetViewports(1, &viewport);

    float rotationAngle = static_cast<float>(
        std::chrono::duration<double>(currentTime.time_since_epoch()).count()) * 0.8f;
    XMMATRIX worldMatrix = XMMatrixRotationY(rotationAngle);

    float camX = g_cameraCtrl.distance * sinf(g_cameraCtrl.yaw) * cosf(g_cameraCtrl.pitch);
    float camY = g_cameraCtrl.distance * sinf(g_cameraCtrl.pitch);
    float camZ = g_cameraCtrl.distance * cosf(g_cameraCtrl.yaw) * cosf(g_cameraCtrl.pitch);

    XMVECTOR eyePosition = XMVectorSet(camX, camY, camZ, 0.0f);
    XMVECTOR lookAtPoint = XMVectorZero();
    XMVECTOR upVector = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX viewMatrix = XMMatrixLookAtLH(eyePosition, lookAtPoint, upVector);
    float aspectRatio = static_cast<float>(g_windowWidth) / static_cast<float>(g_windowHeight);
    XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH(
        XM_PIDIV4, aspectRatio, 0.1f, 100.0f);

    XMMATRIX viewProjMatrix = viewMatrix * projectionMatrix;
    TransformData transformData;
    transformData.worldTransform = XMMatrixTranspose(worldMatrix);
    g_d3dContext->UpdateSubresource(g_transformBuffer, 0, nullptr, &transformData, 0, 0);

    D3D11_MAPPED_SUBRESOURCE mappedRes;
    if (SUCCEEDED(g_d3dContext->Map(g_cameraBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedRes)))
    {
        CameraData* camData = static_cast<CameraData*>(mappedRes.pData);
        camData->viewProjection = XMMatrixTranspose(viewProjMatrix);
        g_d3dContext->Unmap(g_cameraBuffer, 0);
    }

    UINT stride = sizeof(VertexFormat);
    UINT offset = 0;
    g_d3dContext->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);
    g_d3dContext->IASetIndexBuffer(g_indexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_d3dContext->IASetInputLayout(g_inputLayout);
    g_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11Buffer* constantBuffers[] = { g_transformBuffer, g_cameraBuffer };
    g_d3dContext->VSSetConstantBuffers(0, 2, constantBuffers);
    g_d3dContext->VSSetShader(g_vertexShader, nullptr, 0);
    g_d3dContext->PSSetShader(g_pixelShader, nullptr, 0);

    g_d3dContext->DrawIndexed(36, 0, 0);

    g_swapChain->Present(1, 0);
}

void HandleResize(UINT width, UINT height)
{
    if (!g_d3dContext || !g_swapChain || !g_d3dDevice)
        return;

    g_windowWidth = width;
    g_windowHeight = height;

    g_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(g_renderTarget);

    if (FAILED(g_swapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_UNKNOWN, 0)))
        return;
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer)))
    {
        g_d3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
        backBuffer->Release();
    }
}

// Cleanup all resources
void CleanupResources()
{
    if (g_d3dContext)
        g_d3dContext->ClearState();

    SAFE_RELEASE(g_transformBuffer);
    SAFE_RELEASE(g_cameraBuffer);
    SAFE_RELEASE(g_inputLayout);
    SAFE_RELEASE(g_vertexShader);
    SAFE_RELEASE(g_pixelShader);
    SAFE_RELEASE(g_indexBuffer);
    SAFE_RELEASE(g_vertexBuffer);
    SAFE_RELEASE(g_renderTarget);
    SAFE_RELEASE(g_swapChain);

#ifdef _DEBUG
    if (g_d3dDevice)
    {
        ID3D11Debug* debugLayer = nullptr;
        if (SUCCEEDED(g_d3dDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&debugLayer)))
        {
            debugLayer->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            debugLayer->Release();
        }
    }
#endif

    SAFE_RELEASE(g_d3dContext);
    SAFE_RELEASE(g_d3dDevice);
}