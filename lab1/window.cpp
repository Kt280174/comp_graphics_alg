#include "window.h"

const LPCWSTR Window::CLASS_NAME = L"DirectXWindowClass";

Window::Window() :
    m_hWnd(nullptr),
    m_hInstance(nullptr),
    m_Width(1280),
    m_Height(720),
    m_pd3dDevice(nullptr),
    m_pImmediateContext(nullptr),
    m_pSwapChain(nullptr),
    m_pRenderTargetView(nullptr) {
}

Window::~Window() {
    CleanupDirectX();
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    UnregisterClass(CLASS_NAME, m_hInstance);
}

bool Window::Initialize(int width, int height, LPCWSTR title) {
    m_Width = width;
    m_Height = height;
    m_Title = title;
    m_hInstance = GetModuleHandle(nullptr);
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassEx(&wc)) {
        MessageBox(nullptr, L"Cannot register window class", L"Error", MB_OK);
        return false;
    }
    m_hWnd = CreateWindowEx(
        0, CLASS_NAME, m_Title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        m_Width, m_Height,
        nullptr, nullptr,
        m_hInstance, this
    );

    if (!m_hWnd) {
        MessageBox(nullptr, L"Cannot create window", L"Error", MB_OK);
        return false;
    }

    ShowWindow(m_hWnd, SW_SHOW);
    return true;
}

bool Window::ProcessMessages() {
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) return false;
    }
    return true;
}

bool Window::InitDirectX() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = m_Width;
    sd.BufferDesc.Height = m_Height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL selectedFeatureLevel;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE,
        nullptr, createDeviceFlags,
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION, &sd,
        &m_pSwapChain, &m_pd3dDevice,
        &selectedFeatureLevel, &m_pImmediateContext
    );

    if (FAILED(hr)) {
        MessageBox(m_hWnd, L"Cannot create DirectX device", L"Error", MB_OK);
        return false;
    }
    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr)) return false;

    hr = m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) return false;

    m_pImmediateContext->OMSetRenderTargets(1, &m_pRenderTargetView, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<FLOAT>(m_Width);
    vp.Height = static_cast<FLOAT>(m_Height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    m_pImmediateContext->RSSetViewports(1, &vp);

    return true;
}

void Window::RenderFrame() {
    if (!m_pImmediateContext || !m_pRenderTargetView) return;
    m_pImmediateContext->ClearRenderTargetView(m_pRenderTargetView, CLEAR_COLOR);
    m_pSwapChain->Present(1, 0);
}

void Window::ResizeSwapChain(int width, int height) {
    if (!m_pSwapChain || width <= 0 || height <= 0) return;

    m_Width = width;
    m_Height = height;

    if (m_pRenderTargetView) {
        m_pImmediateContext->OMSetRenderTargets(0, nullptr, nullptr);
        m_pRenderTargetView->Release();
        m_pRenderTargetView = nullptr;
    }
    HRESULT hr = m_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr)) return;

    hr = m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) return;

    m_pImmediateContext->OMSetRenderTargets(1, &m_pRenderTargetView, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<FLOAT>(width);
    vp.Height = static_cast<FLOAT>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    m_pImmediateContext->RSSetViewports(1, &vp);
}

void Window::CleanupDirectX() {
    if (m_pImmediateContext) m_pImmediateContext->ClearState();
    if (m_pRenderTargetView) { m_pRenderTargetView->Release(); m_pRenderTargetView = nullptr; }
    if (m_pSwapChain) { m_pSwapChain->Release(); m_pSwapChain = nullptr; }
    if (m_pImmediateContext) { m_pImmediateContext->Release(); m_pImmediateContext = nullptr; }
    if (m_pd3dDevice) { m_pd3dDevice->Release(); m_pd3dDevice = nullptr; }
}

LRESULT CALLBACK Window::WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* pWindow = nullptr;

    if (msg == WM_CREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pWindow = (Window*)pCreate->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pWindow);
    }
    else {
        pWindow = (Window*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    if (pWindow) {
        switch (msg) {
        case WM_SIZE: {
            UINT newWidth = LOWORD(lParam);
            UINT newHeight = HIWORD(lParam);
            if (newWidth > 0 && newHeight > 0) {
                pWindow->ResizeSwapChain(newWidth, newHeight);
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}