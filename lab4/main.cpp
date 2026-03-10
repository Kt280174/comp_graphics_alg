#define WIN32_LEAN_AND_MEAN
#include "D3D11Renderer.h"

// Global renderer instance
D3D11Renderer* g_pRenderer = nullptr;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        if (g_pRenderer && wParam != SIZE_MINIMIZED)
        {
            UINT newW = LOWORD(lParam);
            UINT newH = HIWORD(lParam);
            if (newW > 0 && newH > 0)
                g_pRenderer->Resize(newW, newH);
        }
        return 0;

    case WM_KEYDOWN:
        if (g_pRenderer)
            g_pRenderer->HandleKey((UINT)wParam, true);
        return 0;

    case WM_KEYUP:
        if (g_pRenderer)
            g_pRenderer->HandleKey((UINT)wParam, false);
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
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"D3D11CubeClass";

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    // Create window
    UINT windowWidth = 1280;
    UINT windowHeight = 720;

    RECT rc = { 0, 0, (LONG)windowWidth, (LONG)windowHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;

    HWND hWnd = CreateWindowW(wc.lpszClassName, L"Lab 4 - Textured Cube with Skybox",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        winWidth, winHeight, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Initialize renderer
    g_pRenderer = new D3D11Renderer();
    if (!g_pRenderer->Initialize(hWnd, windowWidth, windowHeight))
    {
        delete g_pRenderer;
        g_pRenderer = nullptr;
        DestroyWindow(hWnd);
        return -1;
    }

    // Message loop
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
            g_pRenderer->Render();
    }

    // Cleanup
    delete g_pRenderer;
    g_pRenderer = nullptr;

    return (int)msg.wParam;
}