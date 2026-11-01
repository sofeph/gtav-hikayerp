#pragma once

#include <d3d11.h>
#include <string>
#include <atomic>

namespace hikaye::hls {

// HLS Player using Media Foundation with injected D3D11 device for zero-copy GPU decoding
class HLSPlayer {
public:
    HLSPlayer();
    ~HLSPlayer();

    // Initialize with injected D3D11 device (called from overlay.cpp EnsureInit)
    static void SetD3D11Device(ID3D11Device* dev, ID3D11DeviceContext* ctx);

    // Open and start playing an HLS stream
    bool Open(const std::string& url, std::string& error);
    
    // Update - call every frame when TV is visible
    void Update();
    
    // Get the shader resource view for rendering
    ID3D11ShaderResourceView* GetSRV() const { return srv_; }
    
    // Check if player is active/playing
    bool IsPlaying() const { return playing_; }
    
    // Stop playback and release resources
    void Stop();

private:
    static ID3D11Device* g_dev_;
    static ID3D11DeviceContext* g_ctx_;
    
    ID3D11ShaderResourceView* srv_ = nullptr;
    ID3D11Texture2D* video_texture_ = nullptr;
    bool playing_ = false;
    std::string current_url_;
    
    // Media Foundation objects (forward declared to avoid header dependencies)
    void* mf_source_reader_ = nullptr;
    void* mf_media_type_ = nullptr;
    
    // Frame timing
    unsigned long long last_frame_time_ = 0;
};

}  // namespace hikaye::hls