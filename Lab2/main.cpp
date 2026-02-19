#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Constants
constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
constexpr float CLEAR_COLOR[] = { 0.12f, 0.12f, 0.16f, 1.0f };

// Vertex structure
struct Vertex {
    float x, y, z;
    uint32_t color;
};

// Vertex data
static const Vertex g_vertices[] = {
    { -0.5f, -0.5f, 0.0f, 0xFF00FF00 },
    {  0.5f, -0.5f, 0.0f, 0xFF0000FF},
    {  0.0f,  0.5f, 0.0f, 0xFFFF0000}
};

static const uint16_t g_indices[] = { 0, 2, 1 };

// Shader code
static const char* g_vsCode = R"(
struct VSInput {
    float3 pos   : POSITION;
    float4 color : COLOR;
};

struct VSOutput {
    float4 pos   : SV_Position;
    float4 color : COLOR;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.pos   = float4(input.pos, 1.0);
    output.color = input.color;
    return output;
}
)";

static const char* g_psCode = R"(
struct PSInput {
    float4 pos   : SV_Position;
    float4 color : COLOR;
};

float4 main(PSInput input) : SV_Target {
    return input.color;
}
)";

// Global D3D11 objects
static ID3D11Device* g_pDevice = nullptr;
static ID3D11DeviceContext* g_pContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_pRTV = nullptr;
static ID3D11VertexShader* g_pVS = nullptr;
static ID3D11PixelShader* g_pPS = nullptr;
static ID3D11InputLayout* g_pInputLayout = nullptr;
static ID3D11Buffer* g_pVertexBuffer = nullptr;
static ID3D11Buffer* g_pIndexBuffer = nullptr;
HRESULT InitD3D11(HWND hwnd);
HRESULT CompileShader(const char* source, const char* entryPoint, const char* target, ID3DBlob** outBlob);
void RenderFrame();
void Cleanup();
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

HRESULT CompileShader(const char* source, const char* entryPoint, const char* target, ID3DBlob** outBlob) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pErrorBlob = nullptr;
    HRESULT hr = D3DCompile(
        source,
        strlen(source),
        nullptr,
        nullptr,
        nullptr,
        entryPoint,
        target,
        flags,
        0,
        outBlob,
        &pErrorBlob
    );

    if (FAILED(hr) && pErrorBlob) {
        OutputDebugStringA((const char*)pErrorBlob->GetBufferPointer());
        MessageBoxA(nullptr, (const char*)pErrorBlob->GetBufferPointer(),
            "Shader Compile Error", MB_ICONERROR);
    }

    if (pErrorBlob) pErrorBlob->Release();
    return hr;
}

HRESULT InitD3D11(HWND hwnd) {
    HRESULT hr;


    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 1;
    scDesc.BufferDesc.Width = WINDOW_WIDTH;
    scDesc.BufferDesc.Height = WINDOW_HEIGHT;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferDesc.RefreshRate.Numerator = 60;
    scDesc.BufferDesc.RefreshRate.Denominator = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hwnd;
    scDesc.SampleDesc.Count = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.Windowed = TRUE;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL featureLevel;


    hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &scDesc,
        &g_pSwapChain,
        &g_pDevice,
        &featureLevel,
        &g_pContext
    );

    if (FAILED(hr)) {

        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createDeviceFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &scDesc,
            &g_pSwapChain,
            &g_pDevice,
            &featureLevel,
            &g_pContext
        );
    }

    if (FAILED(hr)) return hr;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr)) return hr;

    hr = g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRTV);
    pBackBuffer->Release();
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(g_vertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = g_vertices;

    hr = g_pDevice->CreateBuffer(&vbDesc, &vbData, &g_pVertexBuffer);
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(g_indices);
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = g_indices;

    hr = g_pDevice->CreateBuffer(&ibDesc, &ibData, &g_pIndexBuffer);
    if (FAILED(hr)) return hr;

    ID3DBlob* pVSBlob = nullptr;
    hr = CompileShader(g_vsCode, "main", "vs_5_0", &pVSBlob);
    if (FAILED(hr)) return hr;

    hr = g_pDevice->CreateVertexShader(
        pVSBlob->GetBufferPointer(),
        pVSBlob->GetBufferSize(),
        nullptr,
        &g_pVS
    );
    if (FAILED(hr)) {
        pVSBlob->Release();
        return hr;
    }

    ID3DBlob* pPSBlob = nullptr;
    hr = CompileShader(g_psCode, "main", "ps_5_0", &pPSBlob);
    if (FAILED(hr)) {
        pVSBlob->Release();
        return hr;
    }

    hr = g_pDevice->CreatePixelShader(
        pPSBlob->GetBufferPointer(),
        pPSBlob->GetBufferSize(),
        nullptr,
        &g_pPS
    );
    if (FAILED(hr)) {
        pVSBlob->Release();
        pPSBlob->Release();
        return hr;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = g_pDevice->CreateInputLayout(
        layout,
        ARRAYSIZE(layout),
        pVSBlob->GetBufferPointer(),
        pVSBlob->GetBufferSize(),
        &g_pInputLayout
    );

    pVSBlob->Release();
    pPSBlob->Release();

    return hr;
}


void RenderFrame() {
    if (!g_pContext || !g_pSwapChain || !g_pRTV) return;
    g_pContext->ClearRenderTargetView(g_pRTV, CLEAR_COLOR);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(WINDOW_WIDTH);
    viewport.Height = static_cast<float>(WINDOW_HEIGHT);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_pContext->RSSetViewports(1, &viewport);
    g_pContext->OMSetRenderTargets(1, &g_pRTV, nullptr);
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    g_pContext->IASetVertexBuffers(0, 1, &g_pVertexBuffer, &stride, &offset);
    g_pContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    g_pContext->IASetInputLayout(g_pInputLayout);
    g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_pContext->VSSetShader(g_pVS, nullptr, 0);
    g_pContext->PSSetShader(g_pPS, nullptr, 0);
    g_pContext->DrawIndexed(3, 0, 0);
    g_pSwapChain->Present(1, 0);
}

void Cleanup() {

    if (g_pInputLayout) {
        g_pInputLayout->Release();
        g_pInputLayout = nullptr;
    }
    if (g_pPS) {
        g_pPS->Release();
        g_pPS = nullptr;
    }
    if (g_pVS) {
        g_pVS->Release();
        g_pVS = nullptr;
    }
    if (g_pIndexBuffer) {
        g_pIndexBuffer->Release();
        g_pIndexBuffer = nullptr;
    }
    if (g_pVertexBuffer) {
        g_pVertexBuffer->Release();
        g_pVertexBuffer = nullptr;
    }
    if (g_pRTV) {
        g_pRTV->Release();
        g_pRTV = nullptr;
    }
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pContext) {
        g_pContext->Release();
        g_pContext = nullptr;
    }
    if (g_pDevice) {
#ifdef _DEBUG
        ID3D11Debug* pDebug = nullptr;
        if (SUCCEEDED(g_pDevice->QueryInterface(IID_PPV_ARGS(&pDebug)))) {
            pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            pDebug->Release();
        }
#endif
        g_pDevice->Release();
        g_pDevice = nullptr;
    }
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
        }
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"D3D11TriangleWindow";

    if (!RegisterClassEx(&wc)) {
        MessageBox(nullptr, L"Failed to register window class", L"Error", MB_ICONERROR);
        return -1;
    }
    RECT rc = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowEx(
        0,
        L"D3D11TriangleWindow",
        L"Direct3D 11 - Colored Triangle",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) {
        MessageBox(nullptr, L"Failed to create window", L"Error", MB_ICONERROR);
        return -1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    if (FAILED(InitD3D11(hwnd))) {
        MessageBox(hwnd, L"Failed to initialize Direct3D 11", L"Error", MB_ICONERROR);
        Cleanup();
        return -1;
    }

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            RenderFrame();
        }
    }

    // Cleanup
    Cleanup();

    return static_cast<int>(msg.wParam);
}