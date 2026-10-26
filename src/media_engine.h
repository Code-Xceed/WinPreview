#pragma once
#include "d2d_engine.h"
#include <string>

namespace WinPreview {

enum class LogTransformProfile {
    None = 0,
    Rec709_Auto = 1,     // Universal Cinema Rec.709 Tone-Map
    AppleLog = 2,        // Apple Log (iPhone 15/16 Pro ProRes)
    SonySLog3 = 3,       // Sony S-Log3 / S-Gamut3.Cine
    CanonCLog3 = 4,      // Canon C-Log2 / C-Log3
    PanasonicVLog = 5,   // Panasonic V-Log
    DjiDLogM = 6         // DJI D-Log M
};

typedef void libvlc_instance_t;
typedef void libvlc_media_player_t;
typedef void libvlc_media_t;
typedef int64_t libvlc_time_t;

class MediaEngine {
public:
    MediaEngine();
    ~MediaEngine();

    bool Initialize(HWND parentHwnd);
    bool IsAvailable() const { return m_hLibVlc != nullptr; }

    bool OpenMedia(const std::wstring& filePath);
    void CloseMedia();

    void Play();
    void Pause();
    void TogglePlayPause();
    void Stop();

    void Seek(float fraction); // 0.0 to 1.0
    void SeekSeconds(int deltaSeconds);

    int GetVolume() const;
    void SetVolume(int volume); // 0 to 100
    void ToggleMute();
    bool IsMuted() const;

    bool IsPlaying() const;
    libvlc_time_t GetTime() const;     // Milliseconds
    libvlc_time_t GetLength() const;   // Milliseconds
    float GetPosition() const;         // 0.0 to 1.0
    int GetState() const;
    bool IsEnded() const;
    void Restart();

    void Resize(int x, int y, int width, int height);
    void ShowVideoWindow(bool show);
    HWND GetVideoHwnd() const { return m_videoHwnd; }

    bool GetVideoDimensions(unsigned int* outWidth, unsigned int* outHeight);

    std::wstring FormatTime(libvlc_time_t ms) const;

    // Log-to-Rec.709 Cinema Color Tone-Mapping
    void SetLogTransform(LogTransformProfile profile);
    LogTransformProfile GetLogTransform() const { return m_logProfile; }
    void CycleLogTransform();
    const wchar_t* GetLogProfileName(LogTransformProfile profile) const;
    std::wstring GetLogTransformStatusText() const;
    bool IsLogTransformActive() const { return m_logProfile != LogTransformProfile::None; }

private:
    HMODULE m_hLibVlc = nullptr;
    libvlc_instance_t* m_vlc = nullptr;
    libvlc_media_player_t* m_mp = nullptr;
    HWND m_parentHwnd = nullptr;
    HWND m_videoHwnd = nullptr;
    bool m_isMuted = false;
    int m_lastVolume = 80;
    mutable bool m_hasStarted = false;
    mutable float m_pendingSeek = -1.0f;
    LogTransformProfile m_logProfile = LogTransformProfile::None;

    void ApplyLogTransform();
    void CheckPendingSeek() const;

    // VLC function pointers
    libvlc_instance_t* (*fn_libvlc_new)(int argc, const char* const* argv) = nullptr;
    void (*fn_libvlc_release)(libvlc_instance_t* p_instance) = nullptr;
    libvlc_media_t* (*fn_libvlc_media_new_path)(libvlc_instance_t* p_instance, const char* path) = nullptr;
    void (*fn_libvlc_media_release)(libvlc_media_t* p_media) = nullptr;
    libvlc_media_player_t* (*fn_libvlc_media_player_new_from_media)(libvlc_media_t* p_media) = nullptr;
    void (*fn_libvlc_media_player_release)(libvlc_media_player_t* p_mi) = nullptr;
    void (*fn_libvlc_media_player_set_hwnd)(libvlc_media_player_t* p_mi, void* drawable) = nullptr;
    int (*fn_libvlc_media_player_play)(libvlc_media_player_t* p_mi) = nullptr;
    void (*fn_libvlc_media_player_pause)(libvlc_media_player_t* p_mi) = nullptr;
    void (*fn_libvlc_media_player_stop)(libvlc_media_player_t* p_mi) = nullptr;
    int (*fn_libvlc_media_player_is_playing)(libvlc_media_player_t* p_mi) = nullptr;
    int (*fn_libvlc_media_player_get_state)(libvlc_media_player_t* p_mi) = nullptr;
    libvlc_time_t (*fn_libvlc_media_player_get_time)(libvlc_media_player_t* p_mi) = nullptr;
    void (*fn_libvlc_media_player_set_time)(libvlc_media_player_t* p_mi, libvlc_time_t i_time) = nullptr;
    libvlc_time_t (*fn_libvlc_media_player_get_length)(libvlc_media_player_t* p_mi) = nullptr;
    float (*fn_libvlc_media_player_get_position)(libvlc_media_player_t* p_mi) = nullptr;
    void (*fn_libvlc_media_player_set_position)(libvlc_media_player_t* p_mi, float f_pos) = nullptr;
    int (*fn_libvlc_audio_get_volume)(libvlc_media_player_t* p_mi) = nullptr;
    int (*fn_libvlc_audio_set_volume)(libvlc_media_player_t* p_mi, int i_volume) = nullptr;
    int (*fn_libvlc_audio_toggle_mute)(libvlc_media_player_t* p_mi) = nullptr;
    int (*fn_libvlc_audio_get_mute)(libvlc_media_player_t* p_mi) = nullptr;
    int (*fn_libvlc_video_get_size)(libvlc_media_player_t* p_mi, unsigned num, unsigned* px, unsigned* py) = nullptr;
    void (*fn_libvlc_video_set_adjust_int)(libvlc_media_player_t* p_mi, unsigned option, int value) = nullptr;
    void (*fn_libvlc_video_set_adjust_float)(libvlc_media_player_t* p_mi, unsigned option, float value) = nullptr;
    int (*fn_libvlc_video_get_adjust_int)(libvlc_media_player_t* p_mi, unsigned option) = nullptr;
    float (*fn_libvlc_video_get_adjust_float)(libvlc_media_player_t* p_mi, unsigned option) = nullptr;

    static LRESULT CALLBACK VideoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace WinPreview
