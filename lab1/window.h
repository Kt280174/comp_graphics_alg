#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

class Window {
public:
    Window();
    ~Window();

    bool Initialize(int width, int height, LPCWSTR title);
    bool ProcessMessages();
    HWND GetHandle() const { return m_hWnd; }
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }

    // DirectX functions
    bool InitDirectX();
    void RenderFrame();
    void CleanupDirectX();
    void ResizeSwapChain(int width, int height);

private:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Window members
    HWND m_hWnd;
    HINSTANCE m_hInstance;
    int m_Width;
    int m_Height;
    LPCWSTR m_Title;
    static const LPCWSTR CLASS_NAME;
    ID3D11Device* m_pd3dDevice;
    ID3D11DeviceContext* m_pImmediateContext;
    IDXGISwapChain* m_pSwapChain;
    ID3D11RenderTargetView* m_pRenderTargetView;

    // blue
    static constexpr float CLEAR_COLOR[4] = { 0.2f, 0.4f, 0.8f, 1.0f };
};
