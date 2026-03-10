#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace DirectX;

#define SAFE_RELEASE(p) if (p) { (p)->Release(); (p) = nullptr; }

struct TexturedVertex
{
    XMFLOAT3 pos;
    XMFLOAT2 uv;
};

struct ModelConstantBuffer
{
    XMMATRIX model;
};

struct ViewProjConstantBuffer
{
    XMMATRIX vp;
};

inline std::wstring GetPath()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        path = path.substr(0, pos + 1);
    return path;
}

inline UINT DivUp(UINT a, UINT b) { return (a + b - 1) / b; }