#include "audio_manager.hpp"

#include <array>
#include <memory>
#include <string>

namespace inop {
namespace gui {
namespace {

std::size_t cue_index(AudioCue cue) {
    return static_cast<std::size_t>(cue);
}

class FakeAudioBackend final : public AudioBackend {
public:
    bool initialize() noexcept override {
        ++initialize_calls;
        ready = initialize_result;
        return ready;
    }

    void shutdown() noexcept override {
        ++shutdown_calls;
        playing.fill(false);
        ready = false;
    }

    bool load(AudioCue cue, const std::string&, bool loop) noexcept override {
        ++load_calls[cue_index(cue)];
        loops[cue_index(cue)] = loop;
        loaded[cue_index(cue)] = load_result;
        return load_result;
    }

    void play(AudioCue cue) noexcept override {
        ++play_calls[cue_index(cue)];
        playing[cue_index(cue)] = true;
    }

    void stop(AudioCue cue) noexcept override {
        ++stop_calls[cue_index(cue)];
        playing[cue_index(cue)] = false;
    }

    void set_master_volume(float value) noexcept override {
        ++volume_calls;
        volume = value;
    }

    bool is_playing(AudioCue cue) const noexcept override {
        return playing[cue_index(cue)];
    }

    bool initialize_result = true;
    bool load_result = true;
    bool ready = false;
    int initialize_calls = 0;
    int shutdown_calls = 0;
    int volume_calls = 0;
    float volume = 0.0f;
    std::array<int, 3> load_calls{};
    std::array<int, 3> play_calls{};
    std::array<int, 3> stop_calls{};
    std::array<bool, 3> loaded{};
    std::array<bool, 3> playing{};
    std::array<bool, 3> loops{};
};

}

void audio_self_test(const std::function<void(bool, const std::string&)>& check) {
    const std::string fixture = __FILE__;
    auto fake = std::make_unique<FakeAudioBackend>();
    FakeAudioBackend* state = fake.get();
    AudioManager audio(std::move(fake), "inop_selftest_no_audio_directory");
    check(audio.initialize(), "audio manager initializes through its backend");
    check(!audio.available(AudioCue::ButtonClick) && state->load_calls[0] == 0,
          "missing audio assets stay optional and are not loaded");

    check(audio.register_asset(AudioCue::ButtonClick, fixture),
          "a button cue can be registered through the central manager");
    audio.register_asset(AudioCue::ButtonClick, fixture);
    audio.play_button_click();
    audio.play_button_click();
    check(state->load_calls[0] == 1 && state->play_calls[0] == 2,
          "repeated button clicks reuse one loaded asset");

    audio.set_muted(true);
    audio.play_button_click();
    check(state->play_calls[0] == 2, "mute suppresses button playback");
    audio.set_muted(false);
    audio.play_button_click();
    check(state->play_calls[0] == 3, "unmute restores button playback");

    audio.set_master_volume(42);
    check(audio.master_volume() == 42 && state->volume > 0.419f && state->volume < 0.421f,
          "master volume reaches the backend immediately");
    audio.set_master_volume(0);
    audio.play_button_click();
    check(state->play_calls[0] == 3, "zero volume safely suppresses one shot playback");
    audio.set_master_volume(70);

    audio.register_asset(AudioCue::BackgroundMusic, fixture);
    audio.start_background_music();
    check(state->playing[1] && state->loops[1], "background music starts as a looping cue");
    audio.set_muted(true);
    check(!state->playing[1], "mute stops active background music");
    audio.set_muted(false);
    check(state->playing[1], "unmute resumes requested background music");
    audio.stop_background_music();
    check(!state->playing[1], "background music stops on request");

    audio.register_asset(AudioCue::Processing, fixture);
    audio.start_processing_cue();
    check(state->playing[2] && state->loops[2], "processing audio starts as a looping cue");
    audio.stop_processing_cue();
    check(!state->playing[2], "processing audio cannot remain active after stop");

    audio.start_background_music();
    audio.start_processing_cue();
    audio.shutdown();
    check(state->shutdown_calls == 1 && !state->playing[1] && !state->playing[2],
          "audio shutdown clears every active cue");

    auto failing = std::make_unique<FakeAudioBackend>();
    FakeAudioBackend* failed_state = failing.get();
    failing->initialize_result = false;
    AudioManager unavailable(std::move(failing), "inop_selftest_no_audio_directory");
    check(!unavailable.initialize(), "audio backend failure is nonfatal");
    unavailable.play_button_click();
    unavailable.start_background_music();
    unavailable.start_processing_cue();
    check(failed_state->play_calls[0] == 0 && failed_state->play_calls[1] == 0 &&
              failed_state->play_calls[2] == 0,
          "playback requests remain safe after initialization failure");
}

}
}
