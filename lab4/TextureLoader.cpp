#include "TextureLoader.h"

UINT TextureLoader::GetBytesPerBlock(DXGI_FORMAT fmt)
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

bool TextureLoader::LoadDDS(const wchar_t* filename, TextureDesc& desc)
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
        UINT blockWidth = DivUp(width, 4u);
        UINT blockHeight = DivUp(height, 4u);
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
        UINT blockWidth = DivUp(width, 4u);
        UINT blockHeight = DivUp(height, 4u);
        UINT mipSize = blockWidth * blockHeight * GetBytesPerBlock(desc.fmt);

        ReadFile(hFile, pDataPtr, mipSize, &dwBytesRead, NULL);
        pDataPtr += mipSize;

        width = max(1, width / 2);
        height = max(1, height / 2);
    }

    CloseHandle(hFile);
    return true;
}

ID3D11ShaderResourceView* TextureLoader::CreateTexture2D(ID3D11Device* device, const TextureDesc& desc)
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
        UINT blockWidth = DivUp(width, 4u);
        UINT blockHeight = DivUp(height, 4u);
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

ID3D11ShaderResourceView* TextureLoader::CreateCubemap(ID3D11Device* device, const std::wstring* facePaths)
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
            UINT blockWidth = DivUp(faceWidth, 4u);
            UINT blockHeight = DivUp(faceHeight, 4u);
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