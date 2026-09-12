// transform.hpp - the one universal transformer, and the sanitizer that
// rides on it.
//
// This replaces the 49 per language tables in languages.cpp. Those had to
// be written and kept by hand, and they disagreed with one another about
// what a digit meant: a3 was Romanian's breve in one table and a caron in
// another, which is why decoding needed to be told which language it was
// reading. Here every mark has one code of its own, so nothing has to be
// told anything, and one function serves every language at once.
//
// The codes are the shape family table: the first digit is the shape of
// the mark, the second is the variation. 2 is an acute, 21 a double
// acute, 22 an acute below. The full table is in PROGRESS.md, and
// tools/gen_transform_table.py turns it into transform_table.inc, which
// is where the 487 characters this can carry actually live.
//
// Three rules make the output unambiguous to read back:
//
//   - A run of digits after a letter is exactly one whole code. There is
//     no longest match rule and nothing to guess.
//   - A single slash between two codes puts a second mark on the same
//     letter. o75/4 is o with a horn and then a grave, which is what
//     Vietnamese needs and the old scheme could not write at all.
//   - A double slash says the digits after it are a literal number.
//     ma2//5 is a with an acute, then the number 5.
//
// Case is carried too, which the old scheme threw away. Code 0 means
// capital and always comes first: a0 is A, a0/2 is A with an acute.
//
// The sanitizer is not a separate pass. Anything this cannot carry is
// simply not written, so the output is always inside the machine
// alphabet. It says nothing when it drops something.
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

// Any UTF-8 text at all -> lowercase INOP-safe ASCII with the marks
// folded to digits. Takes no language, which is the whole point of it.
//
// Everything it cannot carry is dropped without a word: punctuation, and
// any letter outside the 487 in the table. What survives is a-z, 0-9,
// space and the slash.
std::string transform(const std::string& text);

TransformValidationResult validate_transform_input(const std::string& text);

// The way back, from folded ASCII to readable UTF-8. Nothing it does not
// recognise is touched, so a string that never went through transform()
// comes back as it went in.
//
// Four characters do not survive the round trip, on purpose and by the
// operator's decision: sharp s, ae, oe and the Turkish dotless i are
// letters in their own right rather than a base with a mark, so they are
// spelled out on the way in and stay spelled out on the way back. See
// "special characters BROKEN" in PROGRESS.md.
std::string untransform(const std::string& text);

TransformValidationResult validate_transformed_data(const std::string& text);

// Every (base letter, code) pair the scheme can produce, sorted and
// without repeats. One list, not one per language, which is the whole
// difference between this and what it replaced.
//
// Offline measurement needs the grammar the scheme declares rather than
// one guessed from a sample of text. Nothing in the message path calls
// this.
std::vector<std::pair<char, std::string>> declared_codes();

}  // namespace inop
