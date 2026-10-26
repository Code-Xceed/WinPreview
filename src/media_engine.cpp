#include "media_engine.h"
#include <vector>
#include <iostream>
#include <algorithm>

namespace WinPreview {

LRESULT CALLBACK MediaEngine::VideoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    HWND parent = GetParent(hwnd);

    switch (msg) {
        case WM_MOUSEMOVE:
            if (parent) {
                SendMessage(parent, WM_APP + 101 /* WM_APP_VIDEO_MOUSEMOVE */, wParam, lParam);
            }
            return 0;
        case WM_SETCURSOR:
            if (parent) {
                return SendMessage(parent, WM_SETCURSOR, wParam, lParam);
            }
            break;
        case WM_LBUTTONDOWN:
            if (parent) {
                // Forward single click to parent or toggle play/pause
                SendMessage(parent, WM_COMMAND, MAKEWPARAM(1001 /* ID_MEDIA_PLAY_PAUSE */, 0), 0);
            }
            return 0;
        case WM_LBUTTONDBLCLK:
            if (parent) {
                // Forward double click to toggle fullscreen
                SendMessage(parent, WM_COMMAND, MAKEWPARAM(1002 /* ID_MEDIA_FULLSCREEN */, 0), 0);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (parent) {
                // Forward wheel for volume
                SendMessage(parent, WM_MOUSEWHEEL, wParam, lParam);
            }
            return 0;
        case WM_KEYDOWN:
            if (parent) {
                // Forward keyboard navigation
                SendMessage(parent, WM_KEYDOWN, wParam, lParam);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

MediaEngine::MediaEngine() {}

MediaEngine::~MediaEngine() {
    CloseMedia();
    if (m_vlc && fn_libvlc_release) {
        fn_libvlc_release(m_vlc);
        m_vlc = nullptr;
    }
    if (m_videoHwnd) {
        DestroyWindow(m_videoHwnd);
        m_videoHwnd = nullptr;
    }
    if (m_hLibVlc) {
        FreeLibrary(m_hLibVlc);
        m_hLibVlc = nullptr;
    }
}

bool MediaEngine::Initialize(HWND parentHwnd) {
    m_parentHwnd = parentHwnd;
    if (m_hLibVlc) return true;

    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }

    // Search paths for VLC (local portable bundle first, then system install)
    std::vector<std::wstring> vlcDirs = {
        exeDir + L"\\libvlc",
        exeDir
    };

    WCHAR envBuf[MAX_PATH];
    if (GetEnvironmentVariableW(L"ProgramFiles", envBuf, MAX_PATH) > 0) {
        vlcDirs.push_back(std::wstring(envBuf) + L"\\VideoLAN\\VLC");
    }
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", envBuf, MAX_PATH) > 0) {
        vlcDirs.push_back(std::wstring(envBuf) + L"\\VideoLAN\\VLC");
    }

    std::wstring foundDir;
    for (const auto& dir : vlcDirs) {
        std::wstring dllPath = dir + L"\\libvlc.dll";
        DWORD attribs = GetFileAttributesW(dllPath.c_str());
        if (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY)) {
            foundDir = dir;
            break;
        }
    }

    if (foundDir.empty()) return false;

    // Set DLL directory so libvlccore.dll and plugins are resolved
    SetDllDirectoryW(foundDir.c_str());
    std::wstring fullDllPath = foundDir + L"\\libvlc.dll";
    m_hLibVlc = LoadLibraryW(fullDllPath.c_str());
    SetDllDirectoryW(NULL);

    if (!m_hLibVlc) return false;

    // Resolve functions
    fn_libvlc_new = (libvlc_instance_t*(*)(int, const char* const*))GetProcAddress(m_hLibVlc, "libvlc_new");
    fn_libvlc_release = (void(*)(libvlc_instance_t*))GetProcAddress(m_hLibVlc, "libvlc_release");
    fn_libvlc_media_new_path = (libvlc_media_t*(*)(libvlc_instance_t*, const char*))GetProcAddress(m_hLibVlc, "libvlc_media_new_path");
    fn_libvlc_media_release = (void(*)(libvlc_media_t*))GetProcAddress(m_hLibVlc, "libvlc_media_release");
    fn_libvlc_media_player_new_from_media = (libvlc_media_player_t*(*)(libvlc_media_t*))GetProcAddress(m_hLibVlc, "libvlc_media_player_new_from_media");
    fn_libvlc_media_player_release = (void(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_media_player_release");
    fn_libvlc_media_player_set_hwnd = (void(*)(libvlc_media_player_t*, void*))GetProcAddress(m_hLibVlc, "libvlc_media_player_set_hwnd");
    fn_libvlc_media_player_play = (int(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_media_player_play");
    fn_libvlc_media_player_pause = (void(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_media_player_pause");
    fn_libvlc_media_player_stop = (void(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_media_player_stop");
    fn_libvlc_media_player_is_playing = (int(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_media_player_is_playing");
    fn_libvlc_media_player_get_state = (int(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_media_player_get_state");
    fn_libvlc_media_player_get_time = (libvlc_time_t(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_media_player_get_time");
    fn_libvlc_media_player_set_time = (void(*)(libvlc_media_player_t*, libvlc_time_t))GetProcAddress(m_hLibVlc, "libvlc_media_player_set_time");
    fn_libvlc_media_player_get_length = (libvlc_time_t(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_media_player_get_length");
    fn_libvlc_media_player_get_position = (float(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_media_player_get_position");
    fn_libvlc_media_player_set_position = (void(*)(libvlc_media_player_t*, float))GetProcAddress(m_hLibVlc, "libvlc_media_player_set_position");
    fn_libvlc_audio_get_volume = (int(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_audio_get_volume");
    fn_libvlc_audio_set_volume = (int(*)(libvlc_media_player_t*, int))GetProcAddress(m_hLibVlc, "libvlc_audio_set_volume");
    fn_libvlc_audio_toggle_mute = (int(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_audio_toggle_mute");
    fn_libvlc_audio_get_mute = (int(*)(libvlc_media_player_t*))GetProcAddress(m_hLibVlc, "libvlc_audio_get_mute");
    fn_libvlc_video_get_size = (int(*)(libvlc_media_player_t*, unsigned, unsigned*, unsigned*))GetProcAddress(m_hLibVlc, "libvlc_video_get_size");
    fn_libvlc_video_set_adjust_int = (void(*)(libvlc_media_player_t*, unsigned, int))GetProcAddress(m_hLibVlc, "libvlc_video_set_adjust_int");
    fn_libvlc_video_set_adjust_float = (void(*)(libvlc_media_player_t*, unsigned, float))GetProcAddress(m_hLibVlc, "libvlc_video_set_adjust_float");
    fn_libvlc_video_get_adjust_int = (int(*)(libvlc_media_player_t*, unsigned))GetProcAddress(m_hLibVlc, "libvlc_video_get_adjust_int");
    fn_libvlc_video_get_adjust_float = (float(*)(libvlc_media_player_t*, unsigned))GetProcAddress(m_hLibVlc, "libvlc_video_get_adjust_float");

    if (!fn_libvlc_new || !fn_libvlc_media_player_play || !fn_libvlc_media_player_set_hwnd) {
        FreeLibrary(m_hLibVlc);
        m_hLibVlc = nullptr;
        return false;
    }

    // Lightweight options
    const char* const vlcArgs[] = {
        "--no-video-title-show",
        "--no-snapshot-preview",
        "--quiet",
        "--avcodec-hw=any"
    };
    m_vlc = fn_libvlc_new(sizeof(vlcArgs) / sizeof(vlcArgs[0]), vlcArgs);
    if (!m_vlc) return false;

    // Register child window class
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = VideoWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"WinPreviewVideoSurface";
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);

    // Create hidden child window
    m_videoHwnd = CreateWindowExW(
        0,
        L"WinPreviewVideoSurface",
        NULL,
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 100, 100,
        parentHwnd,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    SetWindowLongPtr(m_videoHwnd, GWLP_USERDATA, (LONG_PTR)this);
    return true;
}

bool MediaEngine::OpenMedia(const std::wstring& filePath) {
    if (!m_vlc) return false;
    CloseMedia();

    // Convert path to UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return false;

    std::string utf8Path(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, filePath.c_str(), -1, &utf8Path[0], utf8Len, NULL, NULL);

    libvlc_media_t* media = fn_libvlc_media_new_path(m_vlc, utf8Path.c_str());
    if (!media) return false;

    m_mp = fn_libvlc_media_player_new_from_media(media);
    fn_libvlc_media_release(media);

    if (!m_mp) return false;

    fn_libvlc_media_player_set_hwnd(m_mp, (void*)m_videoHwnd);
    fn_libvlc_audio_set_volume(m_mp, m_lastVolume);
    m_hasStarted = false;
    m_pendingSeek = -1.0f;

    // Detect if the file is a camera log profile from filename
    std::wstring lowerPath = filePath;
    for (auto& c : lowerPath) c = towlower(c);
    if (m_logProfile == LogTransformProfile::None) {
        if (lowerPath.find(L"apple") != std::wstring::npos && lowerPath.find(L"log") != std::wstring::npos) {
            m_logProfile = LogTransformProfile::AppleLog;
        } else if (lowerPath.find(L"slog") != std::wstring::npos || lowerPath.find(L"s-log") != std::wstring::npos) {
            m_logProfile = LogTransformProfile::SonySLog3;
        } else if (lowerPath.find(L"clog") != std::wstring::npos || lowerPath.find(L"c-log") != std::wstring::npos) {
            m_logProfile = LogTransformProfile::CanonCLog3;
        } else if (lowerPath.find(L"dlog") != std::wstring::npos || lowerPath.find(L"d-log") != std::wstring::npos) {
            m_logProfile = LogTransformProfile::DjiDLogM;
        } else if (lowerPath.find(L"vlog") != std::wstring::npos || lowerPath.find(L"v-log") != std::wstring::npos) {
            m_logProfile = LogTransformProfile::PanasonicVLog;
        } else if (lowerPath.find(L"log") != std::wstring::npos) {
            m_logProfile = LogTransformProfile::Rec709_Auto;
        }
    }
    ApplyLogTransform();

    fn_libvlc_media_player_play(m_mp);

    ShowVideoWindow(true);
    return true;
}

void MediaEngine::CloseMedia() {
    if (m_mp) {
        fn_libvlc_media_player_stop(m_mp);
        fn_libvlc_media_player_release(m_mp);
        m_mp = nullptr;
    }
    m_hasStarted = false;
    m_pendingSeek = -1.0f;
    ShowVideoWindow(false);
}

void MediaEngine::CheckPendingSeek() const {
    if (m_pendingSeek >= 0.0f && m_mp && fn_libvlc_media_player_set_position) {
        if (fn_libvlc_media_player_is_playing && fn_libvlc_media_player_is_playing(m_mp)) {
            fn_libvlc_media_player_set_position(m_mp, m_pendingSeek);
            m_pendingSeek = -1.0f;
        }
    }
}

int MediaEngine::GetState() const {
    if (m_mp && fn_libvlc_media_player_get_state) {
        return fn_libvlc_media_player_get_state(m_mp);
    }
    return 0; // libvlc_NothingSpecial
}

bool MediaEngine::IsEnded() const {
    if (!m_mp) return false;
    if (fn_libvlc_media_player_get_state) {
        int state = fn_libvlc_media_player_get_state(m_mp);
        if (state == 6 /* libvlc_Ended */) return true;
        if (state == 5 /* libvlc_Stopped */ && m_hasStarted) return true;
    }
    if (m_hasStarted && fn_libvlc_media_player_is_playing && !fn_libvlc_media_player_is_playing(m_mp)) {
        if (fn_libvlc_media_player_get_position) {
            float pos = fn_libvlc_media_player_get_position(m_mp);
            if (pos >= 0.985f) return true;
        }
    }
    return false;
}

void MediaEngine::Restart() {
    if (!m_mp) return;
    if (fn_libvlc_media_player_stop) fn_libvlc_media_player_stop(m_mp);
    if (fn_libvlc_media_player_play) fn_libvlc_media_player_play(m_mp);
    m_hasStarted = true;
    m_pendingSeek = -1.0f;
}

void MediaEngine::Play() {
    if (!m_mp) return;
    if (IsEnded()) {
        Restart();
    } else if (fn_libvlc_media_player_play) {
        fn_libvlc_media_player_play(m_mp);
    }
}

void MediaEngine::Pause() {
    if (m_mp && fn_libvlc_media_player_pause) fn_libvlc_media_player_pause(m_mp);
}

void MediaEngine::TogglePlayPause() {
    if (m_mp) {
        if (IsEnded()) {
            Restart();
        } else if (IsPlaying()) {
            Pause();
        } else {
            Play();
        }
    }
}

void MediaEngine::Stop() {
    if (m_mp && fn_libvlc_media_player_stop) fn_libvlc_media_player_stop(m_mp);
}

void MediaEngine::Seek(float fraction) {
    if (!m_mp) return;
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    if (IsEnded()) {
        Restart();
        if (fraction > 0.005f) {
            m_pendingSeek = fraction;
        }
    } else if (fn_libvlc_media_player_set_position) {
        fn_libvlc_media_player_set_position(m_mp, fraction);
    }
}

void MediaEngine::SeekSeconds(int deltaSeconds) {
    if (!m_mp) return;
    libvlc_time_t length = GetLength();
    if (length <= 0) return;

    if (IsEnded()) {
        libvlc_time_t target;
        if (deltaSeconds < 0) {
            target = length + ((libvlc_time_t)deltaSeconds * 1000);
        } else {
            target = (libvlc_time_t)deltaSeconds * 1000;
        }
        if (target < 0) target = 0;
        if (target > length) target = length;
        float frac = (float)target / (float)length;
        Seek(frac);
        return;
    }

    libvlc_time_t current = GetTime();
    libvlc_time_t target = current + ((libvlc_time_t)deltaSeconds * 1000);
    if (target < 0) target = 0;
    if (target > length) target = length;
    if (fn_libvlc_media_player_set_time) {
        fn_libvlc_media_player_set_time(m_mp, target);
    }
}

int MediaEngine::GetVolume() const {
    if (m_mp && fn_libvlc_audio_get_volume) {
        return fn_libvlc_audio_get_volume(m_mp);
    }
    return m_lastVolume;
}

void MediaEngine::SetVolume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    m_lastVolume = volume;
    if (m_mp && fn_libvlc_audio_set_volume) {
        fn_libvlc_audio_set_volume(m_mp, volume);
    }
}

void MediaEngine::ToggleMute() {
    if (m_mp && fn_libvlc_audio_toggle_mute) {
        fn_libvlc_audio_toggle_mute(m_mp);
        m_isMuted = fn_libvlc_audio_get_mute ? fn_libvlc_audio_get_mute(m_mp) != 0 : !m_isMuted;
    }
}

bool MediaEngine::IsMuted() const {
    if (m_mp && fn_libvlc_audio_get_mute) {
        return fn_libvlc_audio_get_mute(m_mp) != 0;
    }
    return m_isMuted;
}

bool MediaEngine::IsPlaying() const {
    if (m_mp && fn_libvlc_media_player_is_playing) {
        CheckPendingSeek();
        int playing = fn_libvlc_media_player_is_playing(m_mp);
        if (playing) {
            m_hasStarted = true;
        }
        return playing != 0;
    }
    return false;
}

libvlc_time_t MediaEngine::GetTime() const {
    CheckPendingSeek();
    if (m_pendingSeek >= 0.0f) {
        return (libvlc_time_t)(m_pendingSeek * (float)GetLength());
    }
    if (IsEnded()) {
        return GetLength();
    }
    if (m_mp && fn_libvlc_media_player_get_time) {
        return fn_libvlc_media_player_get_time(m_mp);
    }
    return 0;
}

libvlc_time_t MediaEngine::GetLength() const {
    if (m_mp && fn_libvlc_media_player_get_length) {
        return fn_libvlc_media_player_get_length(m_mp);
    }
    return 0;
}

float MediaEngine::GetPosition() const {
    CheckPendingSeek();
    if (m_pendingSeek >= 0.0f) {
        return m_pendingSeek;
    }
    if (IsEnded()) {
        return 1.0f;
    }
    if (m_mp && fn_libvlc_media_player_get_position) {
        return fn_libvlc_media_player_get_position(m_mp);
    }
    return 0.0f;
}

void MediaEngine::Resize(int x, int y, int width, int height) {
    if (m_videoHwnd) {
        SetWindowPos(m_videoHwnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void MediaEngine::ShowVideoWindow(bool show) {
    if (m_videoHwnd) {
        ShowWindow(m_videoHwnd, show ? SW_SHOW : SW_HIDE);
    }
}

bool MediaEngine::GetVideoDimensions(unsigned int* outWidth, unsigned int* outHeight) {
    if (m_mp && fn_libvlc_video_get_size) {
        return fn_libvlc_video_get_size(m_mp, 0, outWidth, outHeight) == 0;
    }
    return false;
}

std::wstring MediaEngine::FormatTime(libvlc_time_t ms) const {
    if (ms < 0) ms = 0;
    int totalSec = (int)(ms / 1000);
    int sec = totalSec % 60;
    int min = (totalSec / 60) % 60;
    int hr = totalSec / 3600;

    WCHAR buf[32];
    if (hr > 0) {
        swprintf_s(buf, L"%d:%02d:%02d", hr, min, sec);
    } else {
        swprintf_s(buf, L"%02d:%02d", min, sec);
    }
    return std::wstring(buf);
}

void MediaEngine::ApplyLogTransform() {
    if (!m_mp || !fn_libvlc_video_set_adjust_int) return;

    if (m_logProfile == LogTransformProfile::None) {
        fn_libvlc_video_set_adjust_int(m_mp, 0 /* libvlc_adjust_Enable */, 0);
        return;
    }

    fn_libvlc_video_set_adjust_int(m_mp, 0 /* Enable */, 1);
    if (!fn_libvlc_video_set_adjust_float) return;

    switch (m_logProfile) {
        case LogTransformProfile::Rec709_Auto:
            fn_libvlc_video_set_adjust_float(m_mp, 1 /* Contrast */, 1.52f);
            fn_libvlc_video_set_adjust_float(m_mp, 2 /* Brightness */, 1.02f);
            fn_libvlc_video_set_adjust_float(m_mp, 4 /* Saturation */, 1.65f);
            fn_libvlc_video_set_adjust_float(m_mp, 5 /* Gamma */, 0.86f);
            break;
        case LogTransformProfile::AppleLog:
            fn_libvlc_video_set_adjust_float(m_mp, 1 /* Contrast */, 1.58f);
            fn_libvlc_video_set_adjust_float(m_mp, 2 /* Brightness */, 1.00f);
            fn_libvlc_video_set_adjust_float(m_mp, 4 /* Saturation */, 1.72f);
            fn_libvlc_video_set_adjust_float(m_mp, 5 /* Gamma */, 0.82f);
            break;
        case LogTransformProfile::SonySLog3:
            fn_libvlc_video_set_adjust_float(m_mp, 1 /* Contrast */, 1.68f);
            fn_libvlc_video_set_adjust_float(m_mp, 2 /* Brightness */, 0.96f);
            fn_libvlc_video_set_adjust_float(m_mp, 4 /* Saturation */, 1.82f);
            fn_libvlc_video_set_adjust_float(m_mp, 5 /* Gamma */, 0.78f);
            break;
        case LogTransformProfile::CanonCLog3:
            fn_libvlc_video_set_adjust_float(m_mp, 1 /* Contrast */, 1.48f);
            fn_libvlc_video_set_adjust_float(m_mp, 2 /* Brightness */, 1.04f);
            fn_libvlc_video_set_adjust_float(m_mp, 4 /* Saturation */, 1.58f);
            fn_libvlc_video_set_adjust_float(m_mp, 5 /* Gamma */, 0.88f);
            break;
        case LogTransformProfile::PanasonicVLog:
            fn_libvlc_video_set_adjust_float(m_mp, 1 /* Contrast */, 1.54f);
            fn_libvlc_video_set_adjust_float(m_mp, 2 /* Brightness */, 1.01f);
            fn_libvlc_video_set_adjust_float(m_mp, 4 /* Saturation */, 1.70f);
            fn_libvlc_video_set_adjust_float(m_mp, 5 /* Gamma */, 0.84f);
            break;
        case LogTransformProfile::DjiDLogM:
            fn_libvlc_video_set_adjust_float(m_mp, 1 /* Contrast */, 1.50f);
            fn_libvlc_video_set_adjust_float(m_mp, 2 /* Brightness */, 1.03f);
            fn_libvlc_video_set_adjust_float(m_mp, 4 /* Saturation */, 1.62f);
            fn_libvlc_video_set_adjust_float(m_mp, 5 /* Gamma */, 0.85f);
            break;
        default: break;
    }
}

void MediaEngine::SetLogTransform(LogTransformProfile profile) {
    m_logProfile = profile;
    ApplyLogTransform();
}

void MediaEngine::CycleLogTransform() {
    int next = ((int)m_logProfile + 1) % 7;
    SetLogTransform((LogTransformProfile)next);
}

const wchar_t* MediaEngine::GetLogProfileName(LogTransformProfile profile) const {
    switch (profile) {
        case LogTransformProfile::None: return L"Off (Native Log)";
        case LogTransformProfile::Rec709_Auto: return L"Rec.709 (Auto)";
        case LogTransformProfile::AppleLog: return L"Apple Log → Rec.709";
        case LogTransformProfile::SonySLog3: return L"Sony S-Log3 → Rec.709";
        case LogTransformProfile::CanonCLog3: return L"Canon C-Log3 → Rec.709";
        case LogTransformProfile::PanasonicVLog: return L"Panasonic V-Log → Rec.709";
        case LogTransformProfile::DjiDLogM: return L"DJI D-Log M → Rec.709";
        default: return L"None";
    }
}

std::wstring MediaEngine::GetLogTransformStatusText() const {
    std::wstring s = L"Tone Mapping: ";
    s += GetLogProfileName(m_logProfile);
    return s;
}

} // namespace WinPreview
