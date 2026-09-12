#pragma once

#include <functional>
#include <memory>
#include <string>

namespace inop {
namespace gui {

enum class AudioCue { ButtonClick, BackgroundMusic, Processing };

class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    virtual bool initialize() noexcept = 0;
    virtual void shutdown() noexcept = 0;
    virtual bool load(AudioCue cue, const std::string& path, bool loop) noexcept = 0;
    virtual void play(AudioCue cue) noexcept = 0;
    virtual void stop(AudioCue cue) noexcept = 0;
    virtual void set_master_volume(float volume) noexcept = 0;
    virtual bool is_playing(AudioCue cue) const noexcept = 0;
};

class AudioManager {
public:
    AudioManager();
    explicit AudioManager(std::unique_ptr<AudioBackend> backend,
                          std::string asset_directory = std::string());
    ~AudioManager();

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    bool initialize() noexcept;
    void shutdown() noexcept;
    bool register_asset(AudioCue cue, const std::string& path) noexcept;
    void discover_assets() noexcept;

    void play_button_click() noexcept;
    void start_background_music() noexcept;
    void stop_background_music() noexcept;
    void start_processing_cue() noexcept;
    void stop_processing_cue() noexcept;

    void set_master_volume(int percent) noexcept;
    int master_volume() const noexcept { return master_volume_; }
    void set_muted(bool muted) noexcept;
    bool muted() const noexcept { return muted_; }
    bool available(AudioCue cue) const noexcept;
    bool initialized() const noexcept { return initialized_; }
    const std::string& asset_directory() const noexcept { return asset_directory_; }

private:
    void reconcile_loop(AudioCue cue, bool requested) noexcept;
    bool silent() const noexcept;

    std::unique_ptr<AudioBackend> backend_;
    std::string asset_directory_;
    bool initialized_ = false;
    bool click_available_ = false;
    bool music_available_ = false;
    bool processing_available_ = false;
    bool background_requested_ = false;
    bool processing_requested_ = false;
    bool muted_ = false;
    int master_volume_ = 70;
};

void audio_self_test(const std::function<void(bool, const std::string&)>& check);

}
}
