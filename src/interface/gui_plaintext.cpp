#include "gui_plaintext.hpp"
#include "gui_text_edit.hpp"
#include "transform.hpp"

#include <stdexcept>

namespace inop::gui {

std::string prepare_gui_plaintext(const std::string& text, bool use_transform,
                                         const std::string& language_code) {
    if (use_transform) {
        const auto validation = language_code == "ell" ? validate_greek_input(text) :
                                language_code == "kor" ? validate_hangul_input(text) :
                                                         validate_transform_input(text);
        if (!validation.ok()) {
            size_t position = 1;
            for (size_t at = 0; at < validation.offset && at < text.size(); ++position)
                read_utf8(text, at);
            throw std::runtime_error("Cannot encipher character " + std::to_string(position) +
                ": " + validation.reason +
                ". Unsupported text has no encoding. Edit the message to continue.");
        }
        if (language_code == "ell") return transform_greek(text);
        if (language_code == "kor") return transform_hangul(text);
        return transform(text);
    }
    size_t position = 0;
    for (size_t at = 0; at < text.size();) {
        ++position;
        const auto cp = read_utf8(text, at);
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') continue;
        const bool letter = (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
        const bool digit = cp >= '0' && cp <= '9';
        const bool punctuation = cp >= '!' && cp <= '~' && !letter && !digit;
        if (!letter && !punctuation) {
            throw std::runtime_error("Cannot encipher character " + std::to_string(position) +
                ": unsupported text has no encoding in this suite. Edit the message to continue.");
        }
    }
    return text;
}

std::string restore_gui_plaintext(const std::string& text, bool use_transform,
                                         const std::string& language_code) {
    if (!use_transform) return text;
    if (language_code == "ell") {
        const auto validation = validate_greek_transformed_data(text);
        if (!validation.ok())
            throw std::runtime_error("Cannot display Greek plaintext: " + validation.reason);
        return untransform_greek(text);
    }
    if (language_code == "kor") {
        const auto validation = validate_hangul_transformed_data(text);
        if (!validation.ok())
            throw std::runtime_error("Cannot display Hangul plaintext: " + validation.reason);
        return untransform_hangul(text);
    }
    return untransform(text);
}

}
