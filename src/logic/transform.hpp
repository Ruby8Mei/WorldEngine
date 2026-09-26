#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace inop {

enum class TransformValidationStatus {
    Valid,
    LiteralContent,
    UnsupportedInput,
    InvalidUtf8,
    MalformedData,
};

struct TransformValidationResult {
    TransformValidationStatus status = TransformValidationStatus::Valid;
    std::size_t offset = 0;
    std::string reason;

    bool ok() const { return status == TransformValidationStatus::Valid; }
};

std::string transform(const std::string& text);

TransformValidationResult validate_transform_input(const std::string& text);

std::string untransform(const std::string& text);

TransformValidationResult validate_transformed_data(const std::string& text);

std::string transform_greek(const std::string& text);
std::string untransform_greek(const std::string& text);
TransformValidationResult validate_greek_input(const std::string& text);
TransformValidationResult validate_greek_transformed_data(const std::string& text);

std::string transform_hangul(const std::string& text);
std::string untransform_hangul(const std::string& text);
TransformValidationResult validate_hangul_input(const std::string& text);
TransformValidationResult validate_hangul_transformed_data(const std::string& text);

std::vector<std::pair<char, std::string>> declared_codes();

}
