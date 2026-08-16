// batch.hpp — pure helpers for batch message processing. The interactive
// flow (asking for config source, language, sign-off confirmation per
// message) lives in main.cpp alongside the rest of the UI; what's here is
// the part worth testing without a terminal attached.
#pragma once

#include <string>
#include <vector>

namespace inop {

// Standard 3.5" HD floppy capacity — the cap on a batch input file.
constexpr size_t MAX_BATCH_FILE_BYTES = 1474560;

// Splits batch input on blank-line boundaries. A message may itself span
// several non-blank lines, joined with a space. Leading/trailing blank runs
// produce no empty messages.
std::vector<std::string> split_batch_messages(const std::string& text);

// Reads a batch file whole, refusing anything over MAX_BATCH_FILE_BYTES
// before reading a single byte of it — no partial processing of an
// oversized file.
bool read_batch_file(const std::string& path, std::string& out, std::string* error);

}  // namespace inop
