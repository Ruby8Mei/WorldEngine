#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
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

using namespace inop;

namespace {

struct Args {
    std::vector<std::string> languages;
    int configs = 3;
    int messages = 3;
    std::string out = "log.txt";
    std::string corpus_dir = "benchmark/corpus";
    std::string hamlet;
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
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "inop_benchmark [--languages all|la,en,...] [--configs N] "
                         "[--messages N] [--out log.txt] [--corpus-dir benchmark/corpus] "
                         "[--hamlet path]\n";
            std::exit(0);
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            std::exit(2);
        }
    }
    return a;
}


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
    int n = 5 + static_cast<int>(rng() % 4);
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

std::map<std::string, std::string>& corpus_cache() {
    static std::map<std::string, std::string> c;
    return c;
}

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

}

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    std::vector<std::string> languages = args.languages;
    if (languages.empty())
        for (const auto& l : supported_languages()) languages.push_back(l.code);

    std::ofstream log(args.out);
    if (!log) { std::cerr << "cannot write " << args.out << "\n"; return 1; }
    log << "test_id,language,category,config_index,message_index,input_length,"
           "encrypt_time_us,decrypt_time_us,chars_processed,chars_per_sec,success,"
           "failure_detail\n";

    const Suite& su = suite("38");
    PipelineConfig cfg;

    std::mt19937 rng(0xC0FFEE);
    long long test_id = 0;
    long long total = 0, failed = 0;

    for (const auto& lang : languages) {
        if (!is_supported_language(lang)) {
            std::cerr << "skipping unknown language code: " << lang << "\n";
            continue;
        }
        const std::string& corpus = load_corpus(lang, args.corpus_dir);

        for (const char* category : CATEGORIES) {
            std::string cat = category;
            if (cat == "real_message" && corpus.empty()) continue;

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
                        size_t off = (static_cast<size_t>(mi) * 37) % (corpus.size() + 1);
                        size_t take = std::min<size_t>(400, corpus.size() - off);
                        raw = corpus.substr(off, take);
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
                        std::string folded = fold_diacritics(mark_literal_digits(raw), lang);

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

                        std::string expected = preprocess(folded, alpha);
                        std::replace(expected.begin(), expected.end(), SPACE_SUB, ' ');
                        bool exact = back == expected;

                        std::string human = resubstitute(back, lang);
                        bool roundtrip = fold_diacritics(mark_literal_digits(human), lang) == back;

                        r.success = exact && roundtrip;
                        if (!exact)
                            r.detail = "decrypt mismatch: expected len " +
                                       std::to_string(expected.size()) + ", got len " +
                                       std::to_string(back.size());
                        else if (!roundtrip)
                            r.detail = "resubstitute round-trip mismatch";
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
        std::string folded = fold_diacritics(mark_literal_digits(text), "eng");

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

