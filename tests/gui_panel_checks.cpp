#include "gui_bombe_panel.hpp"
#include "gui_enciphering_panel.hpp"
#include "gui_legal_panel.hpp"

#include <algorithm>
#include <string>

#include "gui_plaintext.hpp"
#include "languages.hpp"
#include "registry.hpp"
#include "transform.hpp"

namespace inop {
namespace gui {

void BombePanel::self_test(
    const std::function<void(bool, const std::string &)> &check) {
  BombePanel panel;
  panel.set_immediate_result(bombe::SearchState::Failure, "Synthetic failure.");
  check(panel.result_text_.find("FAILURE") != std::string::npos &&
            panel.result_text_.find("Synthetic failure.") != std::string::npos,
        "Bombe panel presents an internal failure state");
  panel.set_immediate_result(bombe::SearchState::NoResult,
                             "Synthetic no result.");
  check(panel.result_text_.find("NO RESULT") != std::string::npos,
        "Bombe panel presents a no-result state");
  panel.set_immediate_result(bombe::SearchState::InvalidInput,
                             "Synthetic invalid input.");
  check(panel.result_text_.find("INVALID INPUT") != std::string::npos,
        "Bombe panel presents an invalid-input state");
  panel.result_ = {};
  panel.result_.state = bombe::SearchState::Cancelled;
  panel.result_.tested = 17576;
  panel.result_.total = bombe::approved_batch_positions();
  panel.result_.completed_orders = 1;
  panel.result_.total_orders = bombe::approved_rotor_orders().size();
  panel.rebuild_result_text();
  check(panel.result_text_.find("1 of 60 rotor orders") != std::string::npos,
        "Bombe panel presents batched cancellation progress");
}

void EncipheringPanel::self_test(const std::function<void(bool, const std::string&)>& check) {
    GuiInput enter;
    enter.key_enter = true;
    check(submit_requested(false, enter, true, true),
          "Enter routes from ordinary plaintext focus");
    check(!submit_requested(false, enter, false, true) &&
              !submit_requested(false, enter, true, false),
          "Enter yields when plaintext focus or panel input ownership is absent");
    enter.typed.push_back('\n');
    check(!submit_requested(false, enter, true, true),
          "Enter preserves explicit multiline text entry");
    enter.typed.clear();
    int submit_count = 0;
    if (submit_requested(true, enter, true, true)) ++submit_count;
    check(submit_count == 1, "button activation and panel Enter produce one submit request");

    PanelState state;
    state.reflector_name = "K950";
    state.master_key_text = "aaaaaa";
    state.marker_text = "abcdefghijklmnop";
    for (int i = 0; i < state.rotor_count; ++i) {
        state.rotor_rows[i].rotor_name = "U" + std::to_string(950 + i);
        state.rotor_rows[i].ring_text = "1";
        state.rotor_rows[i].notch_box[0] = std::string(1, static_cast<char>('a' + i));
    }
    EncipheringPanel panel;
    int audio_starts = 0;
    int audio_stops = 0;
    panel.set_processing_audio([&audio_starts] { ++audio_starts; },
                               [&audio_stops] { ++audio_stops; });
    panel.open(state);
    check(panel.open_error_.empty(), "GUI plaintext regression machine opens");
    if (!panel.pipeline_) return;
    const std::string original = "Hello \xC3\x81" "bc \xE1\xBB\x99" "5 a0 123";
    panel.paste_target_ = PasteTarget::Message;
    panel.deliver_paste(original);
    panel.on_encipher();
    check(panel.message_ == original && panel.encipher_error_.empty() &&
          panel.check_out_ == original, "Encipher keeps readable input and untransforms the round-trip check");
    check(audio_starts == 1 && audio_stops == 1,
          "processing audio stops after successful enciphering");
    panel.cipher_in_ = panel.cipher_out_;
    panel.on_decipher();
    check(panel.plain_out_ == original && panel.decipher_error_.empty(),
          "Decipher displays readable plaintext instead of internal codes");
    const std::string generated_cipher = panel.cipher_out_;
    const std::string generated_check = panel.check_out_;
    const std::string generated_plain = panel.plain_out_;
    check(clear_field(panel.message_) && panel.message_.empty() &&
              panel.cipher_in_ == generated_cipher &&
              panel.cipher_out_ == generated_cipher && panel.check_out_ == generated_check,
          "Message Clear keeps ciphertext input and generated output");
    panel.message_ = original;
    check(clear_field(panel.cipher_in_) && panel.cipher_in_.empty() &&
              panel.message_ == original && panel.plain_out_ == generated_plain,
          "Ciphertext Clear keeps message input and generated plaintext");
    panel.cipher_in_ = generated_cipher;
    check(audio_starts == 2 && audio_stops == 2,
          "processing audio stops after successful deciphering");
    const std::string accepted_old_marker = "ponmlkjihgfedcba";
    panel.compatibility_mode_ = true;
    panel.compatibility_marker_ = accepted_old_marker;
    panel.paste_target_ = PasteTarget::CompatibilityMarker;
    panel.deliver_paste(panel.cipher_out_ + "     " + accepted_old_marker);
    check(panel.compatibility_marker_ == accepted_old_marker && !panel.decipher_error_.empty(),
          "combined ciphertext and marker paste is rejected without changing the old marker");
    panel.paste_target_ = PasteTarget::CompatibilityMarker;
    panel.deliver_paste("abcdefghijklmnopq");
    check(panel.compatibility_marker_ == accepted_old_marker && !panel.decipher_error_.empty(),
          "oversized old marker paste is rejected without truncation");
    panel.paste_target_ = PasteTarget::CompatibilityMarker;
    panel.deliver_paste("ponmlkjihgfedcba");
    check(panel.compatibility_marker_ == accepted_old_marker && panel.decipher_error_.empty(),
          "an exact separate old marker paste is accepted");
    panel.compatibility_mode_ = false;
    const std::string prepared = prepare_gui_plaintext(original, true);
    check(prepared == transform(original) && prepared != original,
          "GUI boundary reuses the unchanged transformer representation");
    panel.message_ = "Hello, world!";
    panel.on_encipher();
    check(panel.message_ == "Hello, world!" && !panel.cipher_out_.empty() &&
          panel.encipher_error_.empty() && panel.check_out_ == "Hello world",
          "Punctuation stays editable and is stripped when Encipher processes the message");
    check(audio_starts == 3 && audio_stops == 3,
          "processing audio stops after an enciphering error");
    panel.message_ = "\xED\x95\x9C";
    panel.on_encipher();
    check(!panel.encipher_error_.empty() && panel.message_ == "\xED\x95\x9C",
          "Unsupported scripts remain editable without silent processing loss");
    panel.paste_target_ = PasteTarget::Message;
    panel.deliver_paste(std::string(kFieldCap + 1, 'a'));
    check(panel.message_ == "\xED\x95\x9C" && !panel.encipher_error_.empty(),
          "Oversized message button paste leaves the original text intact");
    check(prepare_gui_plaintext("HELLO!", false) == "HELLO!" &&
              preprocess(prepare_gui_plaintext("HELLO, WORLD!", false), Alphabet("abcdefghijklmnopqrstuvwxyz")) ==
                  "helloworld",
          "Legacy input reaches preprocessing and punctuation never reaches the machine");
    PanelState greek_state = state;
    greek_state.language_code = "ell";
    EncipheringPanel greek_panel;
    greek_panel.open(greek_state);
    const std::string greek = "Θεσσαλονικη ψυχη λογος";
    const std::string greek_internal =
        prepare_gui_plaintext(greek, true, greek_state.language_code);
    greek_panel.paste_target_ = PasteTarget::Message;
    greek_panel.deliver_paste(greek);
    greek_panel.on_encipher();
    check(greek_panel.language_code_ == "ell" &&
              greek_internal == "th0essalonike2 quc3e2 logos",
          "Greek setup selection activates the Greek preprocessing path");
    check(greek_panel.message_ == greek && greek_panel.check_out_ == greek &&
              greek_panel.check_out_.find("th0") == std::string::npos &&
              greek_panel.encipher_error_.empty(),
          "Greek stays visible in the GUI and internal codes stay hidden");
    greek_panel.cipher_in_ = greek_panel.cipher_out_;
    greek_panel.on_decipher();
    check(greek_panel.plain_out_ == greek && greek_panel.decipher_error_.empty(),
          "Greek plaintext completes the GUI cipher round trip");
    greek_panel.message_ = "ά";
    greek_panel.on_encipher();
    check(greek_panel.message_ == "ά" && greek_panel.cipher_out_.empty() &&
              !greek_panel.encipher_error_.empty(),
          "Greek diacritics stay visible when no reversible encoding exists");
    PanelState hangul_state = state;
    hangul_state.language_code = "kor";
    EncipheringPanel hangul_panel;
    hangul_panel.open(hangul_state);
    const std::string hangul = "저는 한국어를 공부해요.";
    const std::string hangul_internal =
        prepare_gui_plaintext(hangul, true, hangul_state.language_code);
    hangul_panel.paste_target_ = PasteTarget::Message;
    hangul_panel.deliver_paste(hangul);
    hangul_panel.on_encipher();
    check(hangul_panel.language_code_ == "kor" && hangul_internal != hangul &&
              hangul_internal.find("/1") != std::string::npos,
          "Korean setup selection activates the framed Hangul preprocessing path");
    check(hangul_panel.message_ == hangul && hangul_panel.check_out_ == hangul &&
              hangul_panel.check_out_.find("/1") == std::string::npos &&
              hangul_panel.encipher_error_.empty(),
          "Hangul stays visible in the GUI and internal codes stay hidden");
    hangul_panel.cipher_in_ = hangul_panel.cipher_out_;
    hangul_panel.on_decipher();
    check(hangul_panel.plain_out_ == hangul && hangul_panel.decipher_error_.empty(),
          "Hangul plaintext completes the GUI cipher round trip");
    hangul_panel.message_ = "ᄀ";
    hangul_panel.on_encipher();
    check(hangul_panel.message_ == "ᄀ" && hangul_panel.cipher_out_.empty() &&
              !hangul_panel.encipher_error_.empty(),
          "Incomplete canonical jamo stays visible when no reversible syllable exists");
    panel.set_processing_audio([] { throw std::runtime_error("audio start failed"); },
                               [] { throw std::runtime_error("audio stop failed"); });
    panel.message_ = original;
    panel.on_encipher();
    check(panel.encipher_error_.empty() && panel.check_out_ == original,
          "audio hook failures do not affect cipher output");
}

void LegalPanel::self_test(const SelfTestCheck& check) {
    LegalPanel panel;
    panel.open(true);
    check(panel.docs_[static_cast<size_t>(panel.selected_)].tab == "EULA" &&
          panel.docs_[static_cast<size_t>(panel.selected_)].text.find("unavailable") != std::string::npos &&
          panel.docs_.back().tab == "Privacy Policy" &&
          panel.docs_.back().text.find("unavailable") != std::string::npos,
          "Legal policy destinations clearly report missing approved content");
    panel.scroll_.back() = 500.0f;
    panel.open();
    check(panel.selected_ == 0 && panel.docs_.front().tab == "INOP" &&
          std::all_of(panel.scroll_.begin(), panel.scroll_.end(), [](float value) { return value == 0; }),
          "Legal licence entry resets selection and document scroll state");
}

}
}
