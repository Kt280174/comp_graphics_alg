#include "window.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Create window
    Window window;
    if (!window.Initialize(1024, 768, L"DirectX 11 - Window")) {
        MessageBox(nullptr, L"Cannot initialize window", L"Error", MB_OK);
        return 1;
    }

    // Initialize DirectX
    if (!window.InitDirectX()) {
        MessageBox(window.GetHandle(), L"Cannot initialize DirectX", L"Error", MB_OK);
        return 1;
    }

    // Main message loop
    bool running = true;
    while (running) {
        running = window.ProcessMessages();
        window.RenderFrame();
    }

    return 0;
}