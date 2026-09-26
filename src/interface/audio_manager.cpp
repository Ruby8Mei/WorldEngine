#include "audio_manager.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace inop {
namespace gui {

namespace {

std::size_t cue_index(AudioCue cue) {
    return static_cast<std::size_t>(cue);
}

bool cue_loops(AudioCue cue) {
    return cue != AudioCue::ButtonClick;
}

const char* cue_base(AudioCue cue) {
    switch (cue) {
        case AudioCue::ButtonClick: return "button-click";
        case AudioCue::BackgroundMusic: return "background-music";
        case AudioCue::Processing: return "processing";
    }
    return "";
}

std::filesystem::path executable_dir() {
#if defined(_WIN32)
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length > 0 && length < path.size())
        return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) == 0)
        return std::filesystem::weakly_canonical(path.data()).parent_path();
#else
    std::vector<char> path(4096);
    const ssize_t length = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length > 0) {
        path[static_cast<std::size_t>(length)] = '\0';
        return std::filesystem::path(path.data()).parent_path();
    }
#endif
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

std::string default_audio_directory() {
    const std::filesystem::path local("audio");
    std::error_code ec;
    if (std::filesystem::is_directory(local, ec)) return local.string();
    const std::filesystem::path beside = executable_dir() / "audio";
    ec.clear();
    if (std::filesystem::is_directory(beside, ec)) return beside.string();
    return local.string();
}

class MiniaudioBackend final : public AudioBackend {
public:
    bool initialize() noexcept override {
        if (ready_) return true;
        ready_ = ma_engine_init(nullptr, &engine_) == MA_SUCCESS;
        return ready_;
    }

    void shutdown() noexcept override {
        for (Slot& slot : slots_) {
            if (!slot.loaded) continue;
            ma_sound_stop(&slot.sound);
            ma_sound_uninit(&slot.sound);
            slot.loaded = false;
        }
        if (ready_) ma_engine_uninit(&engine_);
        ready_ = false;
    }

    bool load(AudioCue cue, const std::string& path, bool loop) noexcept override {
        if (!ready_) return false;
        Slot& slot = slots_[cue_index(cue)];
        if (slot.loaded) {
            ma_sound_stop(&slot.sound);
            ma_sound_uninit(&slot.sound);
            slot.loaded = false;
        }
        const ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
        if (ma_sound_init_from_file(&engine_, path.c_str(), flags, nullptr, nullptr,
                                    &slot.sound) != MA_SUCCESS)
            return false;
        ma_sound_set_looping(&slot.sound, loop ? MA_TRUE : MA_FALSE);
        slot.loaded = true;
        return true;
    }

    void play(AudioCue cue) noexcept override {
        Slot& slot = slots_[cue_index(cue)];
        if (!ready_ || !slot.loaded) return;
        ma_sound_seek_to_pcm_frame(&slot.sound, 0);
        ma_sound_start(&slot.sound);
    }

    void stop(AudioCue cue) noexcept override {
        Slot& slot = slots_[cue_index(cue)];
        if (!ready_ || !slot.loaded) return;
        ma_sound_stop(&slot.sound);
        ma_sound_seek_to_pcm_frame(&slot.sound, 0);
    }

    void set_master_volume(float volume) noexcept override {
        if (ready_) ma_engine_set_volume(&engine_, volume);
    }

    bool is_playing(AudioCue cue) const noexcept override {
        const Slot& slot = slots_[cue_index(cue)];
        return ready_ && slot.loaded && ma_sound_is_playing(&slot.sound) == MA_TRUE;
    }

private:
    struct Slot {
        ma_sound sound{};
        bool loaded = false;
    };

    ma_engine engine_{};
    std::array<Slot, 3> slots_{};
    bool ready_ = false;
};


}

AudioManager::AudioManager()
    : AudioManager(std::make_unique<MiniaudioBackend>(), default_audio_directory()) {}

AudioManager::AudioManager(std::unique_ptr<AudioBackend> backend, std::string asset_directory)
    : backend_(std::move(backend)),
      asset_directory_(asset_directory.empty() ? default_audio_directory()
                                               : std::move(asset_directory)) {}

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize() noexcept {
    if (initialized_) return true;
    if (!backend_ || !backend_->initialize()) {
        std::cerr << "audio: playback initialization failed; continuing without audio\n";
        return false;
    }
    initialized_ = true;
    backend_->set_master_volume(static_cast<float>(master_volume_) / 100.0f);
    discover_assets();
    reconcile_loop(AudioCue::BackgroundMusic, background_requested_);
    reconcile_loop(AudioCue::Processing, processing_requested_);
    return true;
}

void AudioManager::shutdown() noexcept {
    if (!backend_) return;
    background_requested_ = false;
    processing_requested_ = false;
    if (initialized_) backend_->shutdown();
    initialized_ = false;
    click_available_ = false;
    music_available_ = false;
    processing_available_ = false;
}

bool AudioManager::register_asset(AudioCue cue, const std::string& path) noexcept {
    if (!initialized_ || path.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;
    bool* available_flag = nullptr;
    switch (cue) {
        case AudioCue::ButtonClick: available_flag = &click_available_; break;
        case AudioCue::BackgroundMusic: available_flag = &music_available_; break;
        case AudioCue::Processing: available_flag = &processing_available_; break;
    }
    if (*available_flag) return true;
    *available_flag = backend_->load(cue, path, cue_loops(cue));
    if (!*available_flag)
        std::cerr << "audio: could not load " << path << "; continuing without this cue\n";
    return *available_flag;
}

void AudioManager::discover_assets() noexcept {
    if (!initialized_) return;
    static const std::array<const char*, 3> extensions{".wav", ".flac", ".mp3"};
    const std::array<AudioCue, 3> cues{AudioCue::ButtonClick, AudioCue::BackgroundMusic,
                                      AudioCue::Processing};
    for (AudioCue cue : cues) {
        if (available(cue)) continue;
        for (const char* extension : extensions) {
            const std::filesystem::path path =
                std::filesystem::path(asset_directory_) / (std::string(cue_base(cue)) + extension);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec)) continue;
            register_asset(cue, path.string());
            break;
        }
    }
}

void AudioManager::play_button_click() noexcept {
    if (initialized_ && click_available_ && !silent()) backend_->play(AudioCue::ButtonClick);
}

void AudioManager::start_background_music() noexcept {
    background_requested_ = true;
    reconcile_loop(AudioCue::BackgroundMusic, true);
}

void AudioManager::stop_background_music() noexcept {
    background_requested_ = false;
    reconcile_loop(AudioCue::BackgroundMusic, false);
}

void AudioManager::start_processing_cue() noexcept {
    processing_requested_ = true;
    reconcile_loop(AudioCue::Processing, true);
}

void AudioManager::stop_processing_cue() noexcept {
    processing_requested_ = false;
    reconcile_loop(AudioCue::Processing, false);
}

void AudioManager::set_master_volume(int percent) noexcept {
    master_volume_ = std::clamp(percent, 0, 100);
    if (initialized_)
        backend_->set_master_volume(static_cast<float>(master_volume_) / 100.0f);
    reconcile_loop(AudioCue::BackgroundMusic, background_requested_);
    reconcile_loop(AudioCue::Processing, processing_requested_);
}

void AudioManager::set_muted(bool muted) noexcept {
    muted_ = muted;
    reconcile_loop(AudioCue::BackgroundMusic, background_requested_);
    reconcile_loop(AudioCue::Processing, processing_requested_);
}

bool AudioManager::available(AudioCue cue) const noexcept {
    switch (cue) {
        case AudioCue::ButtonClick: return click_available_;
        case AudioCue::BackgroundMusic: return music_available_;
        case AudioCue::Processing: return processing_available_;
    }
    return false;
}

void AudioManager::reconcile_loop(AudioCue cue, bool requested) noexcept {
    if (!initialized_ || !available(cue)) return;
    if (!requested || silent()) {
        if (backend_->is_playing(cue)) backend_->stop(cue);
        return;
    }
    if (!backend_->is_playing(cue)) backend_->play(cue);
}

bool AudioManager::silent() const noexcept {
    return muted_ || master_volume_ == 0;
}


}
}
