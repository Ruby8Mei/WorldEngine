// benchmark_main.cpp — combinatorial correctness/timing harness.
//
// Scoped to the benchmark/test suite ONLY: every test case is a fully
// independent encrypt/decrypt job with its own rotor state, which is what
// makes this workload embarrassingly parallel across messages (never
// within one message — rotor stepping is sequential by design). See
// GPU_FEASIBILITY.md. This file must never be wired into the live message
// pipeline in main.cpp, which is intentionally kept at human-operator
// speed.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "generator.hpp"
#include "inop.hpp"
#include "languages.hpp"
#include "pipeline.hpp"
#include "registry.hpp"
#include "settings.hpp"
#include "transform.hpp"

using namespace inop;

namespace {

struct Args {
    std::vector<std::string> languages;  // empty == all
    int configs = 3;    // x: independent configs per (language, category)
    int messages = 3;   // x: messages per config
    std::string out = "benchmark.csv";
    std::string corpus_dir = "benchmark/corpus";
    std::string hamlet;  // path to a Hamlet corpus file; empty == skip
    bool rotor_motion = false;   // run the rotor-movement survey instead
    int motion_length = 1000;    // message length the survey measures over
    int motion_trials = 8;       // random setups averaged per table cell
};

std::string next_val(int& i, int argc, char** argv) {
    if (i + 1 >= argc) { std::cerr << "missing value for " << argv[i] << "\n"; std::exit(2); }
    return argv[++i];
}

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--languages") {
            std::string v = next_val(i, argc, argv);
            if (v != "all") {
                std::istringstream is(v);
                std::string tok;
                while (std::getline(is, tok, ',')) if (!tok.empty()) a.languages.push_back(tok);
            }
        } else if (arg == "--configs") {
            a.configs = std::stoi(next_val(i, argc, argv));
        } else if (arg == "--messages") {
            a.messages = std::stoi(next_val(i, argc, argv));
        } else if (arg == "--out") {
            a.out = next_val(i, argc, argv);
        } else if (arg == "--corpus-dir") {
            a.corpus_dir = next_val(i, argc, argv);
        } else if (arg == "--hamlet") {
            a.hamlet = next_val(i, argc, argv);
        } else if (arg == "--rotor-motion") {
            a.rotor_motion = true;
        } else if (arg == "--motion-length") {
            a.motion_length = std::stoi(next_val(i, argc, argv));
        } else if (arg == "--motion-trials") {
            a.motion_trials = std::stoi(next_val(i, argc, argv));
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "inop_benchmark [--languages all|la,en,...] [--configs N] "
                         "[--messages N] [--out benchmark.csv] [--corpus-dir benchmark/corpus] "
                         "[--hamlet path]\n"
                         "inop_benchmark --rotor-motion [--motion-length N] "
                         "[--motion-trials N]\n";
            std::exit(0);
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            std::exit(2);
        }
    }
    return a;
}

// ── message categories ──────────────────────────────────────────────────

const char* CATEGORIES[] = {"same_char", "alternating", "cyclic", "jumbled",
                             "real_message", "repeated_phrase", "edge_case"};

std::string cat_same_char(std::mt19937& rng, int len) {
    static const std::string alpha = "abcdefghijklmnopqrstuvwxyz0123456789";
    char c = alpha[rng() % alpha.size()];
    return std::string(static_cast<size_t>(len), c);
}

std::string cat_alternating(std::mt19937& rng, int len) {
    static const std::string alpha = "abcdefghijklmnopqrstuvwxyz";
    char a = alpha[rng() % alpha.size()];
    char b = alpha[rng() % alpha.size()];
    std::string out;
    out.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) out += (i % 2 == 0) ? a : b;
    return out;
}

std::string cat_cyclic(std::mt19937& rng, int len) {
    static const std::string alpha = "abcdefghijklmnopqrstuvwxyz";
    std::string cycle;
    int n = 5 + static_cast<int>(rng() % 4);  // 5-8 symbol cycle
    for (int i = 0; i < n; ++i) cycle += alpha[rng() % alpha.size()];
    std::string out;
    out.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) out += cycle[static_cast<size_t>(i) % cycle.size()];
    return out;
}

std::string cat_jumbled(std::mt19937& rng, int len) {
    static const std::string alpha = "abcdefghijklmnopqrstuvwxyz0123456789 ";
    std::string out;
    out.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) out += alpha[rng() % alpha.size()];
    return out;
}

std::string cat_repeated_phrase(int len) {
    static const std::string phrase = "the quick brown fox jumps ";
    std::string out;
    out.reserve(static_cast<size_t>(len));
    while (static_cast<int>(out.size()) < len) out += phrase;
    out.resize(static_cast<size_t>(len));
    return out;
}

std::vector<std::string> edge_cases() {
    return {
        "",
        "a",
        "0123456789",
        "a1b2c3d4e5f6g7h8",
        std::string(3000, 'z'),
        "room a2 and room b3, meet at 1400 on 28/2/1941",
    };
}

// real_message excerpt sizes, cycled across configs/messages so a single
// run exercises short/medium/long/full-length text per language rather
// than always the same fixed window.
const std::vector<size_t> REAL_MESSAGE_LENGTH_TIERS = {100, 400, 1500, 5000};

// corpus.substr() cuts on raw bytes, and corpora with dense multi-byte
// diacritics (Hindi IAST, Korean romanization, ...) can land a cut in the
// middle of a UTF-8 character — producing an invalid tail byte sequence
// that then fails the resubstitute round-trip. Back the end position off
// to the last complete character boundary instead of truncating
// mid-character. `end` is an absolute offset into `s`.
size_t utf8_safe_end(const std::string& s, size_t end) {
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
    return end;
}

// Same idea for a start offset: don't begin an excerpt on a continuation
// byte either, or the first character in the message is broken too.
size_t utf8_safe_start(const std::string& s, size_t start) {
    while (start < s.size() && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) ++start;
    return start;
}

std::map<std::string, std::string>& corpus_cache() {
    static std::map<std::string, std::string> c;
    return c;
}

// Empty string if no corpus file exists for this language — callers skip
// the real_message category in that case rather than fabricating text.
const std::string& load_corpus(const std::string& lang, const std::string& dir) {
    auto it = corpus_cache().find(lang);
    if (it != corpus_cache().end()) return it->second;
    std::ifstream f(dir + "/" + lang + ".txt", std::ios::binary);
    std::string text;
    if (f) text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return corpus_cache()[lang] = text;
}

std::string csv_escape(const std::string& s) {
    bool needs_quote = s.find_first_of(",\"\n") != std::string::npos;
    if (!needs_quote) return s;
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += '"'; out += c; }
    out += "\"";
    return out;
}

struct Result {
    long long test_id;
    std::string language, category;
    int config_index, message_index;
    size_t input_length;
    double encrypt_us, decrypt_us;
    size_t chars_processed;
    double chars_per_sec;
    bool success;
    std::string detail;
};

void write_row(std::ofstream& log, const Result& r) {
    log << r.test_id << ',' << r.language << ',' << r.category << ',' << r.config_index << ','
        << r.message_index << ',' << r.input_length << ',' << r.encrypt_us << ',' << r.decrypt_us
        << ',' << r.chars_processed << ',' << r.chars_per_sec << ',' << (r.success ? 1 : 0) << ','
        << csv_escape(r.detail) << "\n";
}

Machine machine_from_generated(const GeneratedSettings& g) {
    std::istringstream iss(settings_to_text(g));
    Settings s;
    parse_settings_block(iss, s, nullptr);
    return build_machine(s);
}

// ── rotor movement survey ───────────────────────────────────────────────
//
// How many distinct positions each wheel actually visits while one message
// is being sent. This is the figure a notch count should be chosen against,
// and it is not the same figure as the machine period: the period says how
// long the machine takes to repeat itself, which it never gets near, while
// this says how much of the machine is doing anything at all in the only
// window that exists in practice. A rotor that visits one position over a
// whole message is a static secret permutation, not a moving part.
//
// Positions are read straight off the machine after each character, so this
// measures the real stepping code rather than a model of it.
struct MotionRow {
    std::vector<double> rotor;  // index 0 is the FAST rotor, counting leftward
    double reflector = 0.0;
};

MotionRow measure_motion(const Suite& su, int rotor_count, int notch_count, int len, int trials) {
    MotionRow row;
    row.rotor.assign(static_cast<size_t>(rotor_count), 0.0);
    for (int t = 0; t < trials; ++t) {
        GeneratedSettings g = random_settings(su, rotor_count, 0, notch_count);
        Machine m = machine_from_generated(g);
        const size_t width = static_cast<size_t>(m.alphabet().size());

        std::vector<std::vector<bool>> seen(static_cast<size_t>(rotor_count),
                                             std::vector<bool>(width, false));
        std::vector<bool> refl_seen(width, false);
        auto sample = [&]() {
            for (int i = 0; i < rotor_count; ++i)
                seen[static_cast<size_t>(i)][static_cast<size_t>(
                    m.rotors()[static_cast<size_t>(i)].position())] = true;
            refl_seen[static_cast<size_t>(m.reflector().position())] = true;
        };

        sample();  // the starting position counts as visited
        for (int k = 0; k < len; ++k) {
            m.encipher(std::string(1, 'a'));  // one character, one step
            sample();
        }

        for (int i = 0; i < rotor_count; ++i) {
            int distinct = 0;
            for (bool b : seen[static_cast<size_t>(i)]) if (b) ++distinct;
            // Report fast-rotor-first: rotors_[rotor_count - 1] is the fast
            // one, and "rotor 2 barely moves" is far easier to read than
            // "rotor 9 barely moves" when the count itself varies.
            row.rotor[static_cast<size_t>(rotor_count - 1 - i)] += distinct;
        }
        int refl_distinct = 0;
        for (bool b : refl_seen) if (b) ++refl_distinct;
        row.reflector += refl_distinct;
    }
    for (double& v : row.rotor) v /= trials;
    row.reflector /= trials;
    return row;
}

void run_rotor_motion_survey(int len, int trials) {
    const Suite& su = suite("38");
    std::cout << "rotor movement over a " << len << "-character message\n"
              << "distinct positions visited, mean of " << trials
              << " random INOP-38 setups per row\n"
              << "r1 is the fast rotor; each rotor has " << su.alphabet.size()
              << " positions available\n\n";

    std::cout << " rotors  notches";
    for (int i = 1; i <= su.max_rotors; ++i) std::cout << "     r" << i;
    std::cout << "   reflector\n";

    // The middle row matters: the distinct-notch rule means 5 notches per
    // rotor is only reachable up to 7 rotors (7 * 5 = 35 <= 38), so a sweep
    // of just the two ends would never show the proposed notch count at a
    // high rotor count at all.
    for (int rotor_count : {su.min_rotors, 7, su.max_rotors}) {
        for (int notches = 1; notches <= su.max_notches; ++notches) {
            // random_settings() clamps the notch count to what the
            // alphabet can supply as distinct symbols across the whole
            // machine, so at 10 rotors an ask of 5 really lands on 3.
            // Print what the machine actually got, not what was asked.
            const int effective =
                std::min(notches, static_cast<int>(su.alphabet.size()) / rotor_count);
            MotionRow row = measure_motion(su, rotor_count, notches, len, trials);
            std::cout << std::setw(7) << rotor_count << std::setw(9) << effective;
            if (effective != notches) std::cout << " (asked " << notches << ")";
            else std::cout << "          ";
            for (int i = 0; i < su.max_rotors; ++i) {
                if (i < rotor_count)
                    std::cout << std::setw(7) << std::fixed << std::setprecision(1)
                              << row.rotor[static_cast<size_t>(i)];
                else
                    std::cout << std::setw(7) << "-";
            }
            std::cout << std::setw(12) << std::fixed << std::setprecision(1) << row.reflector
                      << "\n";
        }
        std::cout << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    if (args.rotor_motion) {
        run_rotor_motion_survey(args.motion_length, args.motion_trials);
        return 0;
    }

    std::vector<std::string> languages = args.languages;
    if (languages.empty())
        for (const auto& l : supported_languages()) languages.push_back(l.code);

    std::ofstream log(args.out);
    if (!log) { std::cerr << "cannot write " << args.out << "\n"; return 1; }
    log << "test_id,language,category,config_index,message_index,input_length,"
           "encrypt_time_us,decrypt_time_us,chars_processed,chars_per_sec,success,"
           "failure_detail\n";

    const Suite& su = suite("38");
    PipelineConfig cfg;  // defaults: double pass + padding + moving reflector on

    std::mt19937 rng(0xC0FFEE);
    long long test_id = 0;
    long long total = 0, failed = 0, missing_corpus = 0;

    for (const auto& lang : languages) {
        if (!is_supported_language(lang)) {
            std::cerr << "skipping unknown language code: " << lang << "\n";
            continue;
        }
        const std::string& corpus = load_corpus(lang, args.corpus_dir);
        if (corpus.empty()) {
            std::cerr << "  !! " << lang << ": no corpus file in " << args.corpus_dir
                      << ", real_message category skipped\n";
            ++missing_corpus;
        }

        for (const char* category : CATEGORIES) {
            std::string cat = category;
            if (cat == "real_message" && corpus.empty()) continue;  // nothing to test with

            std::vector<std::string> edges;
            if (cat == "edge_case") edges = edge_cases();

            for (int ci = 0; ci < args.configs; ++ci) {
                GeneratedSettings g = random_settings(su, su.min_rotors, su.max_plug_pairs / 2, 1);
                Machine machine = machine_from_generated(g);
                Pipeline pipe(machine, cfg);
                const Alphabet& alpha = machine.alphabet();

                for (int mi = 0; mi < args.messages; ++mi) {
                    std::string raw;
                    int len = 20 + static_cast<int>(rng() % 200);
                    if (cat == "same_char")        raw = cat_same_char(rng, len);
                    else if (cat == "alternating") raw = cat_alternating(rng, len);
                    else if (cat == "cyclic")      raw = cat_cyclic(rng, len);
                    else if (cat == "jumbled")     raw = cat_jumbled(rng, len);
                    else if (cat == "repeated_phrase") raw = cat_repeated_phrase(len);
                    else if (cat == "real_message") {
                        size_t tier_idx = static_cast<size_t>(ci * args.messages + mi) %
                                          REAL_MESSAGE_LENGTH_TIERS.size();
                        size_t want = REAL_MESSAGE_LENGTH_TIERS[tier_idx];
                        size_t off = utf8_safe_start(
                            corpus, (static_cast<size_t>(mi) * 37) % (corpus.size() + 1));
                        size_t end = utf8_safe_end(corpus, off + std::min(want, corpus.size() - off));
                        raw = corpus.substr(off, end - off);
                    } else if (cat == "edge_case") {
                        raw = edges[static_cast<size_t>(mi) % edges.size()];
                    }

                    Result r;
                    r.test_id = ++test_id;
                    r.language = lang;
                    r.category = cat;
                    r.config_index = ci + 1;
                    r.message_index = mi + 1;
                    r.input_length = raw.size();
                    r.success = false;
                    ++total;

                    try {
                        std::string folded = transform(raw);

                        auto t0 = std::chrono::steady_clock::now();
                        Encrypted e = pipe.encrypt(folded);
                        auto t1 = std::chrono::steady_clock::now();
                        std::string back = pipe.decrypt(e.ciphertext, e.marker);
                        auto t2 = std::chrono::steady_clock::now();

                        r.encrypt_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
                        r.decrypt_us = std::chrono::duration<double, std::micro>(t2 - t1).count();
                        r.chars_processed = folded.size();
                        double total_s = (r.encrypt_us + r.decrypt_us) / 1e6;
                        r.chars_per_sec = total_s > 0 ? static_cast<double>(r.chars_processed) / total_s : 0.0;

                        // Pipeline::decrypt() maps SPACE_SUB back to a real
                        // space before returning — mirror that here, or
                        // every message with a space in it "fails".
                        std::string expected = preprocess(folded, alpha);
                        std::replace(expected.begin(), expected.end(), SPACE_SUB, ' ');
                        bool exact = back == expected;

                        // Folding what untransform() hands back has to
                        // reproduce exactly what went in. That is the one
                        // property an operator depends on, and one call
                        // each way is now the whole of it.
                        std::string human = untransform(back);
                        bool roundtrip = transform(human) == back;

                        r.success = exact && roundtrip;
                        if (!exact)
                            r.detail = "decrypt mismatch: expected len " +
                                       std::to_string(expected.size()) + ", got len " +
                                       std::to_string(back.size());
                        else if (!roundtrip)
                            r.detail = "untransform round trip mismatch";
                    } catch (const std::exception& ex) {
                        r.encrypt_us = r.decrypt_us = 0;
                        r.chars_processed = 0;
                        r.chars_per_sec = 0;
                        r.detail = std::string("exception: ") + ex.what();
                    }

                    if (!r.success) ++failed;
                    write_row(log, r);
                }
            }
        }
        std::cout << "  " << lang << " done\n";
    }

    log.close();
    std::cout << total << " test case(s), " << failed << " failure(s). Log: " << args.out << "\n";
    if (missing_corpus > 0)
        std::cout << "  !! " << missing_corpus << " of " << languages.size()
                  << " language(s) had no real_message corpus — see warnings above\n";

    if (!args.hamlet.empty()) {
        std::ifstream f(args.hamlet, std::ios::binary);
        if (!f) {
            std::cerr << "cannot open Hamlet corpus: " << args.hamlet << "\n";
            return failed > 0 ? 1 : 0;
        }
        std::string text(std::istreambuf_iterator<char>(f), (std::istreambuf_iterator<char>()));

        GeneratedSettings g = random_settings(su, su.min_rotors, su.max_plug_pairs / 2, 1);
        Machine machine = machine_from_generated(g);
        Pipeline pipe(machine, cfg);
        std::string folded = transform(text);

        auto t0 = std::chrono::steady_clock::now();
        Encrypted e = pipe.encrypt(folded);
        auto t1 = std::chrono::steady_clock::now();
        std::string back = pipe.decrypt(e.ciphertext, e.marker);
        auto t2 = std::chrono::steady_clock::now();

        double enc_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double dec_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        std::string expected = preprocess(folded, machine.alphabet());
        std::replace(expected.begin(), expected.end(), SPACE_SUB, ' ');
        bool exact = back == expected;

        std::cout << "\nHamlet demonstration:\n"
                  << "  source chars   " << text.size() << "\n"
                  << "  folded chars   " << folded.size() << "\n"
                  << "  encrypt        " << enc_ms << " ms\n"
                  << "  decrypt        " << dec_ms << " ms\n"
                  << "  exact match    " << (exact ? "yes" : "NO") << "\n";
        if (!exact) { failed += 1; std::cout << "  !! Hamlet round trip did not match exactly\n"; }
    }

    return failed > 0 ? 1 : 0;
}
