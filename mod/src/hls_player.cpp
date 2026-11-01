#include "hls_player.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <d3d11.h>
#include <wrl/client.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "d3d11.lib")

using Microsoft::WRL::ComPtr;

namespace hikaye::hls {

// Static device pointers
ID3D11Device* HLSPlayer::g_dev_ = nullptr;
ID3D11DeviceContext* HLSPlayer::g_ctx_ = nullptr;

void HLSPlayer::SetD3D11Device(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    g_dev_ = dev;
    g_ctx_ = ctx;
}

HLSPlayer::HLSPlayer() {
    // Initialize Media Foundation
    MFStartup(MF_VERSION, MFSTARTUP_FULL);
}

HLSPlayer::~HLSPlayer() {
    Stop();
    MFShutdown();
}

bool HLSPlayer::Open(const std::string& url, std::string& error) {
    if (!g_dev_ || !g_ctx_) {
        error = "D3D11 device not injected. Call SetD3D11Device first.";
        return false;
    }

    Stop(); // Clean up any existing playback
    
    current_url_ = url;
    
    // Convert URL to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wurl(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], wlen);

    // Create Media Foundation source reader for HLS
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFAttributes> attrs;
    
    HRESULT hr = MFCreateAttributes(&attrs, 3);
    if (FAILED(hr)) {
        error = "Failed to create MF attributes";
        return false;
    }
    
    // Enable hardware acceleration with our D3D11 device
    attrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, g_dev_);
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attrs->SetUINT32(MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, TRUE);
    
    hr = MFCreateSourceReaderFromURL(wurl.c_str(), attrs.Get(), &reader);
    if (FAILED(hr)) {
        error = "Failed to create source reader for HLS stream: " + std::to_string(hr);
        return false;
    }
    
    // Set output type to NV12 for hardware decoding
    ComPtr<IMFMediaType> output_type;
    hr = MFCreateMediaType(&output_type);
    if (FAILED(hr)) {
        error = "Failed to create output media type";
        return false;
    }
    
    output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    
    hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output_type.Get());
    if (FAILED(hr)) {
        error = "Failed to set output media type: " + std::to_string(hr);
        return false;
    }
    
    // Get the actual output type to know dimensions
    ComPtr<IMFMediaType> actual_type;
    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual_type);
    if (FAILED(hr)) {
        error = "Failed to get actual media type";
        return false;
    }
    
    UINT32 width = 0, height = 0;
    MFGetAttributeSize(actual_type.Get(), MF_MT_FRAME_SIZE, &width, &height);
    
    if (width == 0 || height == 0) {
        error = "Invalid video dimensions";
        return false;
    }
    
    // Create D3D11 texture for video frames (NV12 format)
    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_NV12;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    tex_desc.CPUAccessFlags = 0;
    tex_desc.MiscFlags = 0;
    
    hr = g_dev_->CreateTexture2D(&tex_desc, nullptr, &video_texture_);
    if (FAILED(hr)) {
        error = "Failed to create video texture: " + std::to_string(hr);
        return false;
    }
    
    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_NV12;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.MostDetailedMip = 0;
    
    hr = g_dev_->CreateShaderResourceView(video_texture_, &srv_desc, &srv_);
    if (FAILED(hr)) {
        error = "Failed to create shader resource view: " + std::to_string(hr);
        video_texture_->Release();
        video_texture_ = nullptr;
        return false;
    }
    
    mf_source_reader_ = reader.Detach();
    mf_media_type_ = output_type.Detach();
    playing_ = true;
    last_frame_time_ = GetTickCount64();
    
    return true;
}

void HLSPlayer::Update() {
    if (!playing_ || !mf_source_reader_ || !video_texture_ || !g_ctx_) {
        return;
    }
    
    // Throttle to ~30 FPS max
    unsigned long long now = GetTickCount64();
    if (now - last_frame_time_ < 33) return;
    last_frame_time_ = now;
    
    ComPtr<IMFSourceReader> reader(static_cast<IMFSourceReader*>(mf_source_reader_));
    
    DWORD stream_flags = 0;
        DWORD stream_index = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
    
        HRESULT hr = reader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &stream_index,
            &stream_flags,
            &timestamp,
            &sample
        );
    
        if (FAILED(hr) || !sample) {
            if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                // End of stream - for live streams this shouldn't happen
                playing_ = false;
            }
            return;
        }
    
    // Get the buffer from the sample
    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr) || !buffer) return;
    
        // Try to get the D3D11 texture directly from the buffer (hardware decoded)
        ComPtr<ID3D11Texture2D> mf_texture;
        hr = buffer->QueryInterface(IID_PPV_ARGS(&mf_texture));
        if (SUCCEEDED(hr) && mf_texture) {
            // Zero-copy: directly use the hardware decoded texture
            g_ctx_->CopyResource(video_texture_, mf_texture.Get());
            return;
        }
    
        // Fallback: lock buffer and copy raw data
        BYTE* data = nullptr;
        DWORD max_len = 0, cur_len = 0;
        hr = buffer->Lock(&data, &max_len, &cur_len);
        if (FAILED(hr) || !data) return;
    
        // Copy NV12 data to texture (simplified - in production use proper NV12 upload)
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = g_ctx_->Map(video_texture_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            // Copy Y plane
            const UINT32 width = 1920, height = 1080;  // Should come from media type
            for (UINT32 row = 0; row < height && row * mapped.RowPitch < cur_len; ++row) {
                memcpy(static_cast<BYTE*>(mapped.pData) + row * mapped.RowPitch,
                       data + row * width, width);
            }
            g_ctx_->Unmap(video_texture_, 0);
        }
        buffer->Unlock();
    }

void HLSPlayer::Stop() {
    playing_ = false;
    
    if (srv_) {
        srv_->Release();
        srv_ = nullptr;
    }
    
    if (video_texture_) {
        video_texture_->Release();
        video_texture_ = nullptr;
    }
    
    if (mf_source_reader_) {
        static_cast<IMFSourceReader*>(mf_source_reader_)->Release();
        mf_source_reader_ = nullptr;
    }
    
    if (mf_media_type_) {
        static_cast<IMFMediaType*>(mf_media_type_)->Release();
        mf_media_type_ = nullptr;
    }
    
    current_url_.clear();
}

}  // namespace hikaye::hls