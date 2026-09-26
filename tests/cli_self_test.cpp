#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "batch.hpp"
#include "cli_compat.hpp"
#include "cli_text.hpp"
#include "cli_self_test.hpp"
#include "generator.hpp"
#include "gui.hpp"
#include "inop.hpp"
#include "languages.hpp"
#include "pipeline.hpp"
#include "registry.hpp"
#include "rng.hpp"
#include "settings.hpp"
#include "transform.hpp"
#include "utf8.hpp"
#include "developer_presets.hpp"

using namespace inop;

namespace {
bool g_test_color = true;
const char* test_color(const char* code) { return g_test_color ? code : ""; }
#define DIM test_color("\033[2m")
#define GREEN test_color("\033[32m")
#define RED test_color("\033[31m")
#define RST test_color("\033[0m")
void rule() {
    std::cout << DIM << "-- " << RST << std::string(60, '-') << "\n";
}
}
int cli_self_test(bool color) {
    g_test_color = color;
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::cout << (ok ? GREEN : RED) << (ok ? "  ok   " : "  FAIL ") << RST << what << "\n";
        // Flushed rather than buffered, because a later check can crash.
        // Removing the floor from random_notches() makes a negative count
        // index off the front of a vector, and the buffered FAIL from the
        // check before it died with the process — which reads as "the
        // guard is not covered" when the truth is the opposite.
        if (!ok) { std::cout.flush(); ++failures; }
    };
    auto build_factory_demo_machine = [](const Settings& s) {
        const Suite& su = suite(s.suite_code);
        Alphabet alpha(su.alphabet);
        std::vector<Rotor> rotors;
        for (size_t i = 0; i < s.rotors.size(); ++i) {
            Rotor rotor = make_rotor(s.rotors[i], alpha);
            if (!su.notches_are_fixed) rotor.set_notches(s.notches[i], alpha);
            rotors.push_back(std::move(rotor));
        }
        return Machine(alpha, std::move(rotors), make_reflector(s.reflector, alpha),
                       Plugboard(s.plugs, alpha), s.rings, s.master_key, su.historic_lock);
    };

    // 1. Historic Enigma vector: rotors I II III, reflector B, all rings 01,
    //    key AAA. Pressing A twelve times gives a known ciphertext.
    {
        Alphabet a(ALPHA26);
        std::vector<Rotor> rs{make_rotor("I", a), make_rotor("II", a), make_rotor("III", a)};
        Machine m(a, std::move(rs), make_reflector("B", a), Plugboard({}, a), {1, 1, 1}, "AAAA", true);
        m.set_moving_reflector(false);
        std::string got = m.encipher("AAAAAAAAAAAA");
        check(got == "BDZGOWCXLTKS", "historic Enigma I-II-III/B vector -> " + got);
    }

    // 2. The machine is its own inverse when rewound.
    {
        Alphabet a(ALPHA38);
        std::vector<Rotor> rs;
        for (auto n : {"R1", "R4", "R7", "R2", "R9"}) {
            Rotor r = make_rotor(n, a);
            r.set_notches("q7#", a);
            rs.push_back(std::move(r));
        }
        Machine m(a, std::move(rs), make_reflector("E", a),
                  Plugboard({"ab", "3x", "#/"}, a), {5, 12, 30, 1, 22}, "k3m9qz", false);
        std::string plain = preprocess("THE QUICK BROWN FOX 0123456789 / END", a);
        m.rewind();
        std::string ct = m.encipher(plain);
        m.rewind();
        std::string back = m.encipher(ct);
        check(back == plain, "reciprocity across 5 rotors + moving reflector");
        check(ct != plain, "ciphertext differs from plaintext");
    }

    // 3. Full pipeline round trip, padding and double pass on.
    {
        Settings s;
        s.suite_code = "38";
        s.rotors = {"R3", "R1", "R8", "R5", "R10"};
        s.reflector = "G";
        s.rings = {7, 19, 2, 33, 11};
        s.notches = {"a", "5", "#", "z", "/"};
        s.plugs = {"qw", "12"};
        s.master_key = "h4t#0p";
        s.marker = "abcdefghijklmnop";
        Machine m = build_factory_demo_machine(s);
        PipelineConfig cfg;
        cfg.marker = s.marker;
        Pipeline p(m, cfg);

        std::string msg = "ATTACK AT DAWN / HOLD THE LINE 0800";
        Encrypted e = p.encrypt(msg);
        std::string back = p.decrypt(e.ciphertext);
        check(back == "attack at dawn / hold the line 0800", "pipeline round trip -> " + back);
        check(e.ciphertext.size() % 16 == 0, "ciphertext is block aligned");
        Encrypted second = p.encrypt(msg);
        check(second.ciphertext != e.ciphertext && p.decrypt(second.ciphertext) == back,
              "fresh cover traffic changes ciphertext while Setup remains reproducible");

        Alphabet alpha(ALPHA38);
        check(setup_marker_valid(s.marker, alpha) &&
                  !setup_marker_valid("abcdefghijklmno", alpha) &&
                  !setup_marker_valid("abcdefghijklmno!", alpha),
              "Setup marker requires exactly 16 active alphabet symbols");
        const std::string framed = frame_with_marker("message", s.marker);
        check(framed.find(s.marker) == 0 && framed.rfind(s.marker) == s.marker.size() + 7,
              "marker copies occupy distinct pre-encipher boundary positions");
        check(!marker_reliability_warning("aaaaaaaaaaaaaaaa").empty() &&
                  marker_reliability_warning(s.marker).empty(),
              "low marker variety produces only a reliability warning");

        bool corrupt_refused = false;
        try {
            p.decrypt(e.ciphertext.substr(1));
        } catch (const std::exception&) {
            corrupt_refused = true;
        }
        check(corrupt_refused, "truncated ciphertext is refused deterministically");

        PipelineConfig old_cfg = cfg;
        old_cfg.marker = "ponmlkjihgfedcba";
        Pipeline old_pipe(m, old_cfg);
        Encrypted old = old_pipe.encrypt(msg);
        Pipeline current_pipe(m, cfg);
        bool wrong_setup_refused = false;
        try {
            current_pipe.decrypt(old.ciphertext);
        } catch (const std::exception&) {
            wrong_setup_refused = true;
        }
        check(wrong_setup_refused &&
                  current_pipe.decrypt_with_marker(old.ciphertext, old_cfg.marker) == back,
              "wrong Setup fails and explicit older marker compatibility succeeds");
    }

    // 4. The double pass removes Enigma's fatal no-self-encipherment property.
    //    The body length here is ODD on purpose. An even length cannot show
    //    the failure this section exists to catch: the transposition applied
    //    between the two passes has to be free of fixed indices, and the old
    //    std::reverse had exactly one whenever the length was odd.
    {
        Settings s;
        s.suite_code = "38";
        s.rotors = {"R1", "R2", "R3", "R4", "R5"};
        s.reflector = "D";
        s.rings = {1, 1, 1, 1, 1};
        s.notches = {"a", "b", "c", "d", "e"};
        s.master_key = "aaaaaa";
        s.marker = "abcdefghijklmnop";
        const size_t odd_len = 4001;

        auto self_hits = [&](bool double_pass) {
            Machine m = build_factory_demo_machine(s);
            PipelineConfig c;
            c.double_pass = double_pass;
            c.padding = false;
            Pipeline p(m, c);
            std::string plain(odd_len, 'a');
            std::string ct = p.encrypt(plain).ciphertext;
            int hits = 0;
            for (size_t i = 0; i < plain.size(); ++i) if (ct[i] == plain[i]) ++hits;
            return hits;
        };
        int single = self_hits(false);
        int doubled = self_hits(true);
        check(single == 0, "single pass: letter never maps to itself (Enigma's flaw), hits=" +
                               std::to_string(single));
        check(doubled > 0, "double pass: self-mapping restored, hits=" + std::to_string(doubled));

        // A chance self-hit is expected and welcome — that is the whole point
        // of the double pass. A STRUCTURAL one is not: an index that
        // self-enciphers under every plaintext is a crib handle at a known
        // position, exactly what Enigma handed Bletchley. Intersecting the
        // self-hit index sets of several unrelated plaintexts separates the
        // two: a 1-in-38 coincidence does not survive sixteen intersections,
        // a structural fixed point survives all of them.
        //
        // The draw alphabet deliberately excludes SPACE_SUB — preprocess()
        // prunes a literal one, which would shorten the body and slide every
        // index after it out of alignment with the plaintext being compared.
        {
            Machine m = build_factory_demo_machine(s);
            PipelineConfig c;
            c.double_pass = true;
            c.padding = false;
            Pipeline p(m, c);
            const std::string draw = "abcdefghijklmnopqrstuvwxyz0123456789/";
            std::vector<bool> universal(odd_len, true);
            for (int trial = 0; trial < 16; ++trial) {
                std::string plain = secure_string(draw, odd_len);
                std::string ct = p.encrypt(plain).ciphertext;
                for (size_t i = 0; i < odd_len; ++i)
                    if (i >= ct.size() || ct[i] != plain[i]) universal[i] = false;
            }
            int structural = 0;
            std::string where;
            for (size_t i = 0; i < odd_len; ++i)
                if (universal[i]) { ++structural; where += " " + std::to_string(i); }
            check(structural == 0,
                  "double pass: no index self-enciphers under every plaintext at an odd length, "
                  "structural fixed points=" + std::to_string(structural) +
                      (where.empty() ? "" : " at index" + where));
        }
    }

    // 5. The Legacy lock: a 1939 machine cannot be given INOP features.
    {
        PipelineConfig c;
        c.double_pass = c.padding = c.moving_reflector = true;
        bool locked = apply_suite_lock(c, suite("26").historic_lock, suite("26").block);
        check(locked && !c.double_pass && !c.padding && !c.moving_reflector && c.block == 5,
              "Legacy locks off double pass, padding and reflector motion; 5-letter blocks");

        PipelineConfig d;
        apply_suite_lock(d, suite("38").historic_lock, suite("38").block);
        check(d.double_pass && d.padding && d.block == 16,
              "INOP-38 keeps its features, 16-symbol blocks");

        Alphabet legacy_alpha(ALPHA26);
        std::vector<Rotor> legacy_rotors{make_rotor("I", legacy_alpha),
                                         make_rotor("II", legacy_alpha),
                                         make_rotor("III", legacy_alpha)};
        Machine legacy_machine(legacy_alpha, std::move(legacy_rotors),
                               make_reflector("B", legacy_alpha), Plugboard({}, legacy_alpha),
                               {1, 1, 1}, "AAAA", true);
        Pipeline legacy_pipe(legacy_machine, c);
        Encrypted legacy_cipher = legacy_pipe.encrypt("legacytestaa");
        check(legacy_pipe.decrypt(legacy_cipher.ciphertext) == "LEGACYTESTAA",
              "Legacy round trip remains marker free");
    }

    // 5b. Stepping rule follows the SUITE, not rotors_.size(). Two 3-rotor
    //     machines with identical wirings, notches, reflector, rings and key
    //     must diverge once one is built as Legacy-style and the other as
    //     INOP-38-style — nothing about "3 rotors" may pick that for them.
    {
        Alphabet a(ALPHA38);
        auto build = [&](bool legacy_stepping) {
            std::vector<Rotor> rs;
            for (auto n : {"R1", "R2", "R3"}) {
                Rotor r = make_rotor(n, a);
                r.set_notches("am", a);
                rs.push_back(std::move(r));
            }
            return Machine(a, std::move(rs), make_reflector("D", a), Plugboard({}, a),
                           {1, 2, 3}, "abcd", legacy_stepping);
        };
        Machine legacy_style = build(true);
        Machine inop38_style = build(false);
        std::string msg(50, 'a');
        std::string ct_legacy = legacy_style.encipher(msg);
        std::string ct_inop38 = inop38_style.encipher(msg);
        check(ct_legacy != ct_inop38,
              "identical 3-rotor wirings/settings diverge between Legacy-style and "
              "INOP-38-style stepping — the rule is chosen by the caller, not inferred");
    }

    // 6. The entropy source must be provably alive.
    {
        bool ok = true;
        std::string why;
        try { entropy_self_check(); } catch (const std::exception& e) { ok = false; why = e.what(); }
        check(ok, ok ? "entropy source is alive and uniform" : why);
    }

    // 7. wiring_is_rotation must rank by the declared alphabet, not ASCII —
    //    ASCII sorts digits/#// before letters, ALPHA38 puts them after.
    {
        auto shift_by_one = [](const std::string& alpha) {
            std::string w;
            w.reserve(alpha.size());
            for (size_t i = 1; i <= alpha.size(); ++i) w += alpha[i % alpha.size()];
            return w;
        };
        check(wiring_is_rotation(shift_by_one(ALPHA38), ALPHA38),
              "shift-by-1 wiring of ALPHA38 is caught as a rotation");
        check(wiring_is_rotation(shift_by_one(ALPHA26), ALPHA26),
              "shift-by-1 wiring of ALPHA26 is caught as a rotation");
        // R1's factory wiring, copied from registry.cpp — must NOT be
        // flagged as a rotation.
        const std::string r1 = "bxml2uokh3#46705cyg19etfprid8swqavnzj/";
        check(!wiring_is_rotation(r1, ALPHA38), "built-in R1 wiring is accepted, not a rotation");
    }

    // 8. Throughput.
    {
        Alphabet a(ALPHA38);
        std::vector<Rotor> rs;
        for (auto n : {"R1", "R2", "R3", "R4", "R5"}) rs.push_back(make_rotor(n, a));
        for (auto& r : rs) r.set_notches("am", a);
        Machine m(a, std::move(rs), make_reflector("D", a), Plugboard({}, a), {1, 2, 3, 4, 5}, "abcdef", false);
        std::string text(200000, 'a');
        auto t0 = std::chrono::steady_clock::now();
        volatile size_t sink = m.encipher(text).size();
        (void)sink;
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << DIM << "  --   " << RST << "throughput: "
                  << static_cast<long>(text.size() / ms / 1000.0) << "M symbols/s\n";
    }

    // 9. One real sentence per language, through the transformer and back.
    //
    //    These are the worked examples the old per-language scheme was
    //    tested against, kept because they are real text in 48 real
    //    languages and no set of invented cases covers as much. What is
    //    checked is different, though. The old test compared against a
    //    hand written expected folding, one per language, which the new
    //    scheme changes and which nobody could maintain by hand anyway.
    //
    //    What is checked here instead is stability: fold a sentence,
    //    unfold it, fold it again, and the two foldings must be the same
    //    string. That is the property an operator actually depends on --
    //    decode a message, encode it again, get the same ciphertext -- and
    //    it fails loudly on any disagreement between the two directions.
    //    It is not circular: nothing here is generated from the
    //    transformer, and a decoder that guessed wrong would produce a
    //    different second folding.
    //
    //    The character level correctness is section 10s job, where every
    //    one of the 480 carried characters is compared against the real
    //    unicode character rather than against the transformer.
    {
        struct Row { const char* lang; const char* plain; };
        static const Row rows[] = {
            {"sqi", "\x55\x6e\xc3\xab\x20\x66\x6c\x61\x73\x20\x73\x68\x71\x69\x70\x20\x64\x68\x65\x20\x70\x69\x20\xc3\xa7\x61\x6a\x2e"},
            {"eus", "\x4b\x61\x69\x78\x6f\x2c\x20\x7a\x65\x72\x20\x6d\x6f\x64\x75\x7a\x20\x7a\x61\x75\x64\x65\x3f"},
            {"bos", "\xc4\x86\x61\x6f\x2c\x20\xc4\x90\x6f\x72\xc4\x91\x65\x20\x76\x6f\x6c\x69\x20\xc4\x8d\x6f\x6b\x6f\x6c\x61\x64\x75\x20\x69\x20\xc4\x8d\x61\x6a\x2e"},
            {"yue", "\x4e\xc3\xa9\x69\x68\x20\x68\xc3\xb3\x75\x2c\x20\x6e\x67\xc3\xb3\x68\x20\x64\xc5\x8d\x75\x20\x68\xc3\xb3\x75\x2e"},
            {"cat", "\x45\x6c\x20\x70\x61\x72\x61\x6c\x6c\x65\x6c\x20\xc3\xa9\x73\x20\x63\x6c\x61\x72\x2e"},
            {"cpf", "\x4c\x69\x20\x66\xc3\xb2\x2c\x20\x6c\x69\x20\x67\x65\x6e\x20\x6b\xc3\xa8\x20\x6b\x6f\x6e\x74\x61\x6e\x2c\x20\x65\x20\x6c\x69\x20\x72\x65\x74\x65\x20\x62\xc3\xb2\x20\x6c\x61\x6e\x6d\xc3\xa8\x20\x61\x2e"},
            {"hrv", "\xc4\x86\x61\x6f\x2c\x20\xc4\x90\x6f\x72\xc4\x91\x65\x20\x76\x6f\x6c\x69\x20\xc4\x8d\x6f\x6b\x6f\x6c\x61\x64\x75\x20\x69\x20\xc4\x8d\x61\x6a\x2e"},
            {"czr", "\x44\xc4\x9b\x6b\x75\x6a\x69\x2c\x20\x6d\xc5\xaf\x6a\x20\x70\xc5\x99\xc3\xad\x74\x65\x6c\x20\x6d\xc3\xa1\x20\x6e\x6f\x76\xc3\xbd\x20\x64\xc5\xaf\x6d\x2e"},
            {"dan", "\x48\xc3\xa5\x70\x65\x72\x20\x64\x75\x20\x66\xc3\xa5\x72\x20\x65\x6e\x20\x66\x69\x6e\x20\x64\x61\x67\x20\x70\xc3\xa5\x20\xc3\xb8\x79\x61\x2c\x20\x6b\x6a\xc3\xa6\x72\x65\x20\x76\x65\x6e\x6e\x2e"},
            {"nld", "\x44\x65\x20\x63\x6f\xc3\xb6\x72\x64\x69\x6e\x61\x74\x69\x65\x20\x77\x61\x73\x20\x69\x64\x65\x65\xc3\xab\x6e\x20\x77\x61\x61\x72\x64\x2e"},
            {"eng", "\x54\x68\x65\x20\x6e\x61\xc3\xaf\x76\x65\x20\x63\x61\x66\xc3\xa9\x20\x6f\x77\x6e\x65\x72\x20\x73\x6d\x69\x6c\x65\x64\x2e"},
            {"est", "\x53\xc3\xb6\xc3\xb6\x64\x61\x76\x20\xc3\xb5\x75\x6e\x61\x70\x75\x75\x20\x6f\x6e\x20\x68\x65\x61\x2e"},
            {"fin", "\x48\xc3\xa4\x6e\x20\x6f\x6e\x20\x74\xc3\xa4\xc3\xa4\x6c\x6c\xc3\xa4\x2e"},
            {"fra", "\x4c\x65\x20\x63\x61\x66\xc3\xa9\x20\x65\x73\x74\x20\x74\x72\xc3\xa8\x73\x20\x63\x68\x65\x72\x2e"},
            {"deu", "\x4d\xc3\xb6\x63\x68\x74\x65\x6e\x20\x53\x69\x65\x20\x65\x69\x6e\x20\x67\x72\x6f\xc3\x9f\x65\x73\x20\x4b\xc3\xa4\x73\x65\x62\x72\xc3\xb6\x74\x63\x68\x65\x6e\x3f"},
            {"hin", "\x4d\x61\x69\xe1\xb9\x83\x20\x4b\xe1\xb9\x9b\xe1\xb9\xa3\xe1\xb9\x87\x61\x20\x6b\xc4\xab\x20\x67\xc4\xab\x74\xc4\x81\x20\x70\x61\xe1\xb9\x9b\x68\x74\xc4\x81\x20\x68\xc5\xab\xe1\xb9\x83\x2e"},
            {"hun", "\xc5\x90\x20\x73\x7a\x65\x72\x65\x74\x69\x20\x61\x20\x67\x79\xc3\xbc\x6d\xc3\xb6\x6c\x63\x73\xc3\xb6\x74\x20\xc3\xa9\x73\x20\x61\x20\x74\xc5\xb1\x7a\x68\x65\x6c\x79\x65\x74\x2e"},
            {"ibo", "\xe1\xbb\x8a\x20\x62\xe1\xbb\xa5\x20\x65\x7a\x69\x67\x62\x6f\x20\xe1\xbb\xa5\x6d\xe1\xbb\xa5\x20\x6e\x77\x6f\x6b\x65\x2e"},
            {"ind", "\x53\x65\x6c\x61\x6d\x61\x74\x20\x70\x61\x67\x69\x2c\x20\x61\x70\x61\x20\x6b\x61\x62\x61\x72\x3f"},
            {"gle", "\x54\xc3\xa1\x20\x6d\x6f\x20\x6d\x68\xc3\xa1\x74\x68\x61\x69\x72\x20\x61\x67\x20\x69\x74\x68\x65\x20\xc3\xba\x6c\x6c\x20\x73\x61\x20\x67\x68\x61\x69\x72\x64\xc3\xad\x6e\x2e"},
            {"ita", "\x50\x65\x72\x63\x68\xc3\xa9\x20\xc3\xa8\x20\x63\x6f\x73\xc3\xac\x20\x63\x69\x74\x74\xc3\xa0\x3f"},
            {"kor", "\x41\x6e\x6e\x79\x65\x6f\x6e\x67\x68\x61\x73\x65\x79\x6f\x2c\x20\x6a\x61\x6c\x20\x6a\x69\x6e\x61\x65\x73\x65\x79\x6f\x3f"},
            {"kmr", "\x45\x7a\x20\x6b\x75\x72\x64\xc3\xae\x20\x6d\x65\x20\xc3\xbb\x20\x6a\x69\x20\xc3\xa7\x61\x79\xc3\xaa\x20\x68\x65\x7a\x20\x64\x69\x6b\x69\x6d\x2c\x20\x6e\x65\x20\x6a\x69\x20\xc5\x9f\x65\x72\xc3\xae\x2e"},
            {"lat", "\x56\xc4\x93\x6e\xc4\xab\x2c\x20\x76\xc4\xab\x64\xc4\xab\x2c\x20\x76\xc4\xab\x63\xc4\xab"},
            {"lit", "\xc4\x96\x6a\x61\x75\x20\x70\x72\x69\x65\x20\xc4\x85\xc5\xbe\x75\x6f\x6c\x6f\x20\x73\x75\x20\xc5\xab\x6b\x69\x6e\x69\x6e\x6b\x75\x2e"},
            {"ltz", "\x4c\xc3\xab\x74\x7a\x65\x62\x75\x65\x72\x67\x20\x61\x73\x73\x20\x65\x20\x73\x63\x68\xc3\xa9\x69\x6e\x74\x20\x4c\x61\x6e\x64\x2e"},
            {"mly", "\x53\x65\x6c\x61\x6d\x61\x74\x20\x70\x61\x67\x69\x2c\x20\x61\x70\x61\x20\x6b\x68\x61\x62\x61\x72\x3f"},
            {"mlt", "\xc4\xa0\x6f\x72\xc4\xa1\x20\x6a\x69\x65\x6b\x6f\x6c\x20\xc4\x8b\x65\x72\x61\x73\x61\x2c\x20\x75\x20\xc5\xbc\x6d\x69\x65\x6e\x20\x68\x75\x77\x61\x20\x73\x61\x62\x69\xc4\xa7\x2e"},
            {"cmn", "\x57\xc7\x92\x20\x68\xc4\x9b\x6e\x20\x78\xc7\x90\x68\x75\xc4\x81\x6e\x20\x7a\x68\xc3\xa8\x67\x65\x20\x64\xc3\xac\x66\xc4\x81\x6e\x67\x2e"},
            {"mri", "\x4b\x65\x69\x20\x74\x65\x20\x70\x61\x69\x20\x74\x65\x20\x72\xc4\x81\x2c\x20\x65\x20\x68\x6f\x61\x20\x6d\xc4\x81\x2e"},
            {"cnr", "\xc5\x9a\x65\x76\x65\x72\x20\x69\x20\xc5\xba\x65\x6e\x69\x63\x61\x20\x73\x75\x20\xc5\x9b\x75\x74\x72\x61\x2e"},
            {"nor", "\x48\xc3\xa5\x70\x65\x72\x20\x64\x75\x20\x66\xc3\xa5\x72\x20\x65\x6e\x20\x66\x69\x6e\x20\x64\x61\x67\x20\x70\xc3\xa5\x20\xc3\xb8\x79\x61\x2c\x20\x6b\x6a\xc3\xa6\x72\x65\x20\x76\x65\x6e\x6e\x2e"},
            {"pol", "\x44\x7a\x69\xc4\x99\x6b\x75\x6a\xc4\x99\x2c\x20\x6d\xc3\xb3\x6a\x20\x77\x75\x6a\x65\x6b\x20\x6d\x61\x20\xc5\x82\x61\x64\x6e\x79\x20\x64\x6f\x6d\x2e\x20\xc4\x86\x6d\x61\x20\x69\x20\xc5\xba\x72\x65\x62\x69\xc4\x99\x20\xc5\x9b\x70\x69\xc4\x85\x2c\x20\x61\x20\xc5\x82\xc4\x85\x6b\x61\x20\x70\x61\x63\x68\x6e\x69\x65\x20\x72\xc3\xb3\xc5\xbc\xc4\x85\x2e"},
            {"por", "\x4f\x20\x69\x72\x6d\xc3\xa3\x6f\x20\x63\x6f\x6d\x65\x75\x20\x70\xc3\xa3\x6f\x20\x63\x6f\x6d\x20\x6d\x61\xc3\xa7\xc3\xa3\x2e"},
            {"ron", "\x43\xc3\xa2\x69\x6e\x65\x6c\x65\x20\x6d\x65\x75\x20\x61\x6c\x65\x61\x72\x67\xc4\x83\x20\xc3\xae\x6e\x20\x67\x72\xc4\x83\x64\x69\x6e\xc4\x83\x2e"},
            {"gla", "\x43\x68\xc3\xac\x20\x6d\x69\x20\x62\xc3\xa0\x74\x61\x20\xc3\xb9\x72\x20\x61\x67\x75\x73\x20\x74\x68\x61\x20\x65\x20\x6d\x61\x74\x68\x2e"},
            {"srp", "\xc4\x86\x61\x6f\x2c\x20\xc4\x90\x6f\x72\xc4\x91\x65\x20\x76\x6f\x6c\x69\x20\xc4\x8d\x6f\x6b\x6f\x6c\x61\x64\x75\x20\x69\x20\xc4\x8d\x61\x6a\x2e"},
            {"svk", "\x4d\xc3\xb4\x6a\x20\x70\x72\x69\x61\x74\x65\xc4\xbe\x20\x6d\xc3\xa1\x20\x6e\x6f\x76\xc3\xbd\x20\x64\x6f\x6d\x20\x76\x20\x6d\x65\x73\x74\x65\x2e"},
            {"slv", "\xc5\xa0\x6c\x61\x20\x73\x65\x6d\x20\x76\x20\x4c\x6a\x75\x62\x6c\x6a\x61\x6e\x6f\x20\x76\x69\x64\x65\x74\x69\x20\xc4\x8d\x75\x64\x6f\x76\x69\x74\x6f\x20\x72\x65\x6b\x6f\x2e"},
            {"som", "\x4e\x61\x62\x61\x64\x2c\x20\x73\x69\x64\x65\x65\x20\x74\x61\x68\x61\x79\x3f"},
            {"spa", "\x45\x6c\x20\x6e\x69\xc3\xb1\x6f\x20\x63\x6f\x6d\x69\xc3\xb3\x20\x70\x69\xc3\xb1\x61\x20\x65\x6e\x20\x45\x73\x70\x61\xc3\xb1\x61\x2e"},
            {"swa", "\x48\x61\x62\x61\x72\x69\x2c\x20\x75\x6e\x61\x65\x6e\x64\x65\x6c\x65\x61\x6a\x65\x3f"},
            {"swe", "\xc3\x85\x73\x61\x20\xc3\xa4\x74\x65\x72\x20\xc3\xa4\x70\x70\x6c\x65\x6e\x20\x6f\x63\x68\x20\x64\x72\x69\x63\x6b\x65\x72\x20\xc3\xb6\x6c\x2e"},
            {"tgl", "\x50\x69\x6e\x75\x6e\x74\x61\x68\x61\x6e\x20\x6e\x61\x6d\x69\x6e\x20\x61\x6e\x67\x20\x50\x65\xc3\xb1\x61\x66\x72\x61\x6e\x63\x69\x61\x2e"},
            {"tur", "\x47\xc3\xbc\x7a\x65\x6c\x20\x62\x69\x72\x20\x67\xc3\xbc\x6e\x2c\x20\x64\x65\xc4\x9f\x69\x6c\x20\x6d\x69\x3f\x20\x49\xc5\x9f\xc4\xb1\x6b\x20\xc3\xa7\x6f\x6b\x20\x70\x61\x72\x6c\x61\x6b\x2e"},
            {"cym", "\x4d\x61\x65\x27\x72\x20\x74\xc5\xb7\x27\x6e\x20\x68\x61\x72\x64\x64\x20\x61\x27\x72\x20\x63\xc5\xb5\x6e\x20\x79\x6e\x20\x68\x61\x70\x75\x73\x2e"},
            {"yor", "\xe1\xba\xb8\x20\xe1\xb9\xa3\x65\x75\x6e\x2c\x20\xe1\xbb\x8d\x6d\xe1\xbb\x8d\x20\x6d\x69\x20\x64\xc3\xa1\x72\x61\x2e"},
            {"zul", "\x53\x61\x77\x75\x62\x6f\x6e\x61\x2c\x20\x75\x6e\x6a\x61\x6e\x69\x3f"},
        };
        int unstable = 0, leftover = 0;
        for (const Row& r : rows) {
            const std::string folded = transform(r.plain);
            const std::string human = untransform(folded);
            if (transform(human) != folded) {
                ++unstable;
                check(false, std::string("transformer not stable for ") + r.lang + ": " + folded +
                                 " -> " + human + " -> " + transform(human));
            }
            // Nothing readable should still be carrying a mark digit. A
            // code left behind means the decoder walked past one, which
            // the stability check alone can miss when both directions are
            // wrong in the same way.
            for (std::size_t i = 1; i < human.size(); ++i) {
                const char prev = human[i - 1];
                const bool prev_is_letter = (prev >= 'a' && prev <= 'z') ||
                                            (prev >= 'A' && prev <= 'Z');
                if (prev_is_letter && human[i] >= '0' && human[i] <= '9') {
                    ++leftover;
                    check(false, std::string("undecoded mark digit left in ") + r.lang + ": " +
                                     human);
                    break;
                }
            }
        }
        check(unstable == 0, "48 real sentences fold, unfold and fold again to the same string");
        check(leftover == 0, "no mark digit survives into readable text");

        // Four of those sentences with the answer written out by hand, so
        // that the stability check above is anchored to something a person
        // read rather than only to itself. Punctuation is gone because the
        // machine cannot carry it, and the case is back because the new
        // scheme carries it.
        struct Fixed { const char* folded_from; const char* human; };
        static const Fixed fixed[] = {
            // Albanian: e with diaeresis, c with cedilla.
            {"\x55\x6e\xc3\xab\x20\x66\x6c\x61\x73\x20\x73\x68\x71\x69\x70\x2e",
             "\x55\x6e\xc3\xab\x20\x66\x6c\x61\x73\x20\x73\x68\x71\x69\x70"},
            // German: three umlauts and a sharp s, which is the one that
            // does not come back.
            {"\x47\x72\x6f\xc3\x9f\x65\x73\x20\x4b\xc3\xa4\x73\x65\x62\x72\xc3\xb6\x74\x63\x68\x65\x6e\x21",
             "\x47\x72\x6f\x73\x73\x65\x73\x20\x4b\xc3\xa4\x73\x65\x62\x72\xc3\xb6\x74\x63\x68\x65\x6e"},
            // Polish: l with stroke, and a with ogonek.
            {"\x4c\xc4\x85\x6b\x61\x20\x69\x20\xc5\x82\xc4\x85\x6b\x61",
             "\x4c\xc4\x85\x6b\x61\x20\x69\x20\xc5\x82\xc4\x85\x6b\x61"},
            // Romanian: the comma below that the old scheme deleted.
            {"\xc8\x98\x69\x20\xc8\x9b\x61\x72\x61",
             "\xc8\x98\x69\x20\xc8\x9b\x61\x72\x61"},
        };
        for (const Fixed& f : fixed) {
            const std::string got = untransform(transform(f.folded_from));
            check(got == f.human, std::string("hand checked round trip -> ") + got);
        }
    }


    // 10. The universal transformer, which is what replaced the 48
    //     per-language tables. Two halves: the classic cases every
    //      language actually needs, and a set built to break it.
    //
    //      It takes no language. That is the point of it, and it is also
    //      what makes this shorter than the old per-language tests: one answer per
    //      character, rather than one per language per character.
    {
        // -- the classic cases -------------------------------------------
        struct Row { const char* plain; const char* folded; };
        static const Row rows[] = {
            {"The naive cafe owner smiled.", "t0he naive cafe owner smiled"},
            {"El nino comio pina en Espana.", "e0l nino comio pina en e0spana"},

            // The nine shape families, one letter each, lowercase so the
            // case code stays out of the way.
            {"\xc4\x81", "a1"},                  // macron
            {"\xc3\xa1", "a2"},                  // acute
            {"\xc7\x8e", "a3"},                  // caron
            {"\xc3\xa0", "a4"},                  // grave
            {"\xc3\xa2", "a5"},                  // circumflex
            {"\xc3\xa3", "a6"},                  // tilde
            {"\xc4\x83", "a7"},                  // breve
            {"\xc4\x8b", "c8"},                  // dot above
            {"\xc5\xaf", "u9"},                  // ring above

            // The second digit of a family: the variations.
            {"\xc5\x91", "o21"},                 // double acute
            {"\xc3\xa7", "c73"},                 // cedilla
            {"\xc4\x85", "a74"},                 // ogonek
            {"\xe1\xba\xb9", "e81"},             // dot below
            {"\xc3\xbc", "u82"},                 // diaeresis
            {"\xc3\xb8", "o12"},                 // stroke, which does not decompose
            {"\xc5\x82", "l12"},                 // the same stroke on another letter

            // Romanian comma-below, which the old scheme deleted outright
            // and this one carries.
            {"\xc8\x99", "s42"},
            {"\xc8\x9b", "t42"},

            // Two marks on one letter. Mandarin needs it, and Vietnamese
            // needs it on a letter the old scheme could not write at all.
            {"\xc7\x96", "u82/1"},               // u diaeresis + macron
            {"\xc7\x9c", "u82/4"},               // u diaeresis + grave
            {"\xe1\xba\xbf", "e5/2"},            // e circumflex + acute
            {"\xe1\xbb\x9d", "o75/4"},           // o horn + grave
            {"\xe1\xbb\xb1", "u75/81"},          // u horn + dot below

            // Case, which the old scheme threw away entirely. The code
            // always sits directly behind the letter it belongs to.
            {"A", "a0"},
            {"\xc3\x81", "a0/2"},                // A with acute
            {"\xc7\x95", "u0/82/1"},             // capital U diaeresis + macron
            {"INOP", "i0n0o0p0"},

            // Literal digits, and the double slash that keeps them apart
            // from a mark.
            {"Room A2", "r0oom a0//2"},
            {"Chateau Latour 1964", "c0hateau l0atour 1964"},
            {"m\xc3\xa1" "5", "ma2//5"},         // a mark and then a number
            {"a12", "a//12"},
            {"1964 and 1918", "1964 and 1918"},
        };
        int classic_failed = 0;
        for (const Row& r : rows) {
            const std::string got = transform(r.plain);
            if (got != r.folded) {
                ++classic_failed;
                check(false, std::string("transform(") + r.plain + ") -> " + got + ", wanted " +
                                 r.folded);
            }
        }
        check(classic_failed == 0,
              "transformer classic cases: " + std::to_string(sizeof(rows) / sizeof(rows[0])) +
                  " rows");

        // -- the adversarial cases ---------------------------------------
        //
        // Nothing here is a plausible message. That is the point of it.
        struct Bad { const char* in; const char* out; const char* why; };
        static const Bad bad[] = {
            {"", "", "empty input"},
            {"   ", "", "nothing but spaces"},
            {"\xc3\xa9", "e2", "one accented letter and nothing else"},
            {"a\xc3\xa9", "ae2", "a bare letter touching an accented one"},
            {"\xc3\xa9" "9", "e2//9", "a number right behind a mark"},
            {"9\xc3\xa9", "9e2", "a number right in front of one"},
            {"a//b", "ab", "a double slash typed by hand is dropped as punctuation"},
            {"a/b", "ab", "and so is a single one"},
            {"!@#$%^&*()", "", "punctuation only, all of it dropped"},
            {"\xe4\xbd\xa0\xe5\xa5\xbd", "", "chinese characters, none of them latin"},
            {"\xd0\xbf\xd1\x80\xd0\xb8", "", "cyrillic, likewise"},
            {"a\xf0\x9f\x98\x80" "b", "ab", "an emoji between two letters"},
            {"a\xc3", "a", "a lead byte with its tail cut off"},
            {"a\xbf" "b", "ab", "a continuation byte with no lead"},
            {"\xff\xfe", "", "bytes that are not utf-8 at all"},
            {"a\n\nb", "a b", "two newlines read as one space"},
            {"a \t b", "a b", "mixed whitespace reads as one space"},
            {" a ", "a", "leading and trailing space trimmed"},
            {"\xc3\x9f", "ss", "sharp s spelled out, and lost"},
            {"\xc3\xa6", "ae", "ae spelled out, and lost"},
            {"\xc4\xb1", "i", "turkish dotless i, flattened to a plain i"},
            {"\xc4\xb0", "i0/8", "turkish capital i with a dot survives whole"},
            {"I", "i0", "a plain capital I is not the turkish one"},
        };
        int bad_failed = 0;
        for (const Bad& b : bad) {
            const std::string got = transform(b.in);
            if (got != b.out) {
                ++bad_failed;
                check(false, std::string("transform edge case (") + b.why + ") -> " + got +
                                 ", wanted " + b.out);
            }
        }
        check(bad_failed == 0, "transformer adversarial cases: " +
                                   std::to_string(sizeof(bad) / sizeof(bad[0])) + " rows");
        check(untransform("A0") == "A0", "unknown uppercase transformer code stays unchanged");
        check(transform("\xc1\x81\xc0\xb5").empty(),
              "overlong UTF-8 letters and digits are rejected");

        check(validate_transform_input("Lowercase 42\n").status ==
                  TransformValidationStatus::Valid,
              "supported transformer input validates explicitly");
        check(validate_transform_input("word!").status ==
                  TransformValidationStatus::Valid &&
                  transform("Wait... what, now?!") == "w0ait what now",
              "punctuation is accepted for deliberate preprocessing removal");
        bool punctuation_removed = true;
        for (char c = '!'; c <= '~'; ++c) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9'))
                continue;
            const std::string symbol(1, c);
            punctuation_removed = punctuation_removed &&
                                  validate_transform_input(symbol).status ==
                                      TransformValidationStatus::Valid &&
                                  transform(symbol).empty();
        }
        check(punctuation_removed,
              "every printable ASCII punctuation symbol is removed before machine processing");
        check(validate_transform_input("\xf0\x28\x8c\x28").status ==
                  TransformValidationStatus::InvalidUtf8,
              "invalid UTF-8 is distinct from unsupported input");
        check(validate_transformed_data("lowercase a0 a2 o75/4 a//42").status ==
                  TransformValidationStatus::Valid,
              "combined transformed data validates explicitly");
        check(validate_transformed_data("A0").status ==
                  TransformValidationStatus::LiteralContent,
              "literal text resembling a control code stays distinct");
        const std::vector<std::string> malformed_transform_data = {
            "a/", "a/2", "a//", "a//x", "a2/", "a2/x", "a31", "a999", "a2/999"};
        bool malformed_reported = true;
        for (const std::string& encoded : malformed_transform_data)
            malformed_reported = malformed_reported &&
                                 validate_transformed_data(encoded).status ==
                                     TransformValidationStatus::MalformedData &&
                                 untransform(encoded) == encoded;
        check(malformed_reported,
              "malformed transformer markers are reported and preserved without repair");

        bool declared_codes_valid = true;
        for (const std::pair<char, std::string>& code : declared_codes()) {
            const std::string encoded = std::string(1, code.first) + code.second;
            declared_codes_valid = declared_codes_valid &&
                                   validate_transformed_data(encoded).status ==
                                       TransformValidationStatus::Valid &&
                                   transform(untransform(encoded)) == encoded;
        }
        check(declared_codes_valid,
              "every declared transformer code validates and round trips");

        bool finite_code_space_valid = true;
        const std::vector<std::pair<char, std::string>> codes = declared_codes();
        for (char base = 'a'; base <= 'z'; ++base) {
            for (int value = 0; value <= 99; ++value) {
                const std::string digits = std::to_string(value);
                bool declared = digits == "0";
                for (const std::pair<char, std::string>& code : codes)
                    if (code.first == base && code.second == digits) declared = true;
                const TransformValidationStatus actual =
                    validate_transformed_data(std::string(1, base) + digits).status;
                finite_code_space_valid = finite_code_space_valid &&
                                          actual == (declared ? TransformValidationStatus::Valid
                                                             : TransformValidationStatus::MalformedData);
            }
        }
        check(finite_code_space_valid,
              "finite single modifier transformer space rejects every undeclared code");
        const std::string deterministic_sample = "\xc3\x81rvíztűrő 1964";
        check(transform(deterministic_sample) == transform(deterministic_sample),
              "repeated transformer output is deterministic");

        // -- the round trip ----------------------------------------------
        //
        // Every character the table carries, taken back the other way.
        // Exhaustive rather than a sample: this is the check that says the
        // scheme is reversible at all.
        int trip_checked = 0, trip_failed = 0;
        for (unsigned cp = 0x00C0; cp <= 0x1EFF; ++cp) {
            std::string ch;
            if (cp < 0x800) {
                ch += static_cast<char>(0xC0 | (cp >> 6));
                ch += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                ch += static_cast<char>(0xE0 | (cp >> 12));
                ch += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                ch += static_cast<char>(0x80 | (cp & 0x3F));
            }
            const std::string folded = transform(ch);
            if (folded.empty()) continue;  // not a character this scheme carries
            // The four spelled out ones are known not to come back. Naming
            // them here is what stops this test quietly growing more.
            if (folded == "ss" || folded == "s0s0" || folded == "ae" || folded == "a0e0" ||
                folded == "oe" || folded == "o0e0" || folded == "i")
                continue;
            ++trip_checked;
            const std::string back = untransform(folded);
            if (back != ch || transform(back) != folded) {
                ++trip_failed;
                if (trip_failed <= 5)
                    check(false, "transformer round trip failed at codepoint " +
                                     std::to_string(cp) + ", folded " + folded);
            }
        }
        check(trip_failed == 0, "transformer round trip: every one of " +
                                    std::to_string(trip_checked) +
                                    " carried characters comes back whole");

        // Whole sentences, which are the only thing that exercises the
        // case code, the marks, the double slash and the spaces at once.
        auto trip = [&](const std::string& text, const std::string& expected) {
            const std::string folded = transform(text);
            const std::string back = untransform(folded);
            check(back == expected, "sentence round trip: " + text + " -> " + folded + " -> " +
                                        back);
        };
        trip("The naive cafe owner smiled.", "The naive cafe owner smiled");
        trip("Room A2 costs 1964 crowns.", "Room A2 costs 1964 crowns");
        trip("\xc4\x90or\xc4\x91" "e voli \xc4\x8dokoladu.",
             "\xc4\x90or\xc4\x91" "e voli \xc4\x8dokoladu");
        trip("Ti\xe1\xba\xbfng Vi\xe1\xbb\x87t", "Ti\xe1\xba\xbfng Vi\xe1\xbb\x87t");
    }

    {
        struct GreekCase { const char* plain; const char* encoded; };
        static const GreekCase cases[] = {
            {"α", "a"}, {"β", "b"}, {"γ", "g"}, {"δ", "d"}, {"ε", "e"},
            {"ζ", "z"}, {"η", "e2"}, {"θ", "th"}, {"ι", "i"}, {"κ", "k"},
            {"λ", "l"}, {"μ", "m"}, {"ν", "n"}, {"ξ", "x"}, {"ο", "o"},
            {"π", "p"}, {"ρ", "r"}, {"σ", "s"}, {"τ", "t"}, {"υ", "u"},
            {"φ", "f"}, {"χ", "c3"}, {"ψ", "q"}, {"ω", "o2"}, {"ς", "s"},
        };
        bool mapped = true;
        for (const GreekCase& row : cases)
            mapped = mapped && transform_greek(row.plain) == row.encoded;
        check(mapped, "every Greek base letter uses the declared mapping");

        const std::string lowercase = "αβγδεζηθικλμνξοπρστυφχψω";
        check(untransform_greek(transform_greek(lowercase)) == lowercase,
              "every mapped Greek letter round trips in word position");
        check(transform_greek("θτ") == "tht" && untransform_greek("tht") == "θτ",
              "Greek theta and tau remain distinct");
        check(transform_greek("ψ") == "q" && untransform_greek("q") == "ψ",
              "Greek psi uses q and round trips");
        check(transform_greek("πσ") == "ps" && untransform_greek("ps") == "πς" &&
                  untransform_greek("ps") != "ψ",
              "Greek pi and sigma never become psi");
        check(transform_greek("εη") == "ee2" && untransform_greek("ee2") == "εη",
              "Greek epsilon and eta remain distinct");
        check(transform_greek("οω") == "oo2" && untransform_greek("oo2") == "οω",
              "Greek omicron and omega remain distinct");
        check(transform_greek("χ") == "c3" && untransform_greek("c3") == "χ",
              "Greek chi remains distinct");
        check(transform_greek("σος") == "sos" && untransform_greek("sos") == "σος" &&
                  transform_greek("σοσ") == "sos",
              "Greek sigma is restored from deterministic word position");

        const std::string uppercase = "ΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩ";
        check(untransform_greek(transform_greek(uppercase)) == uppercase,
              "uppercase Greek round trips");
        check(transform_greek("ε2 η2") == "e//2 e2//2" &&
                  untransform_greek("e//2 e2//2") == "ε2 η2",
              "Greek literal digits remain distinct from letter codes");
        check(validate_greek_transformed_data("h").status ==
                  TransformValidationStatus::MalformedData,
              "standalone h is invalid Greek transformed data");
        check(validate_greek_input("ά").status == TransformValidationStatus::UnsupportedInput,
              "unsupported Greek diacritics are reported without transformation");
        check(is_supported_language("ell"), "Greek is available through language selection");
        check(transform("theta") == "theta" && untransform("theta") == "theta",
              "existing Latin transformer behavior is unchanged");
    }

    {
        struct HangulCase { const char* plain; const char* encoded; };
        static const HangulCase consonants[] = {
            {"ㄱ", "/3g/"}, {"ㄴ", "/3n/"}, {"ㄷ", "/3d/"}, {"ㄹ", "/3l/"},
            {"ㅁ", "/3m/"}, {"ㅂ", "/3b/"}, {"ㅅ", "/3s/"}, {"ㅇ", "/3q/"},
            {"ㅈ", "/3j/"}, {"ㅊ", "/3ch/"}, {"ㅋ", "/3k/"}, {"ㅌ", "/3t/"},
            {"ㅍ", "/3p/"}, {"ㅎ", "/3h/"},
        };
        bool consonants_mapped = true;
        for (const HangulCase& row : consonants)
            consonants_mapped = consonants_mapped && transform_hangul(row.plain) == row.encoded &&
                untransform_hangul(row.encoded) == row.plain;
        check(consonants_mapped, "every primitive Hangul consonant uses the declared mapping");

        static const HangulCase vowels[] = {
            {"ㅏ", "/3a/"}, {"ㅑ", "/3ya/"}, {"ㅓ", "/3eo/"}, {"ㅕ", "/3yeo/"},
            {"ㅗ", "/3o/"}, {"ㅛ", "/3yo/"}, {"ㅜ", "/3u/"}, {"ㅠ", "/3yu/"},
            {"ㅡ", "/3eu/"}, {"ㅣ", "/3i/"},
        };
        bool vowels_mapped = true;
        for (const HangulCase& row : vowels)
            vowels_mapped = vowels_mapped && transform_hangul(row.plain) == row.encoded &&
                untransform_hangul(row.encoded) == row.plain;
        check(vowels_mapped, "every primitive Hangul vowel uses the declared mapping");

        static const HangulCase tense[] = {
            {"ㄲ", "/3gg/"}, {"ㄸ", "/3dd/"}, {"ㅃ", "/3bb/"},
            {"ㅆ", "/3ss/"}, {"ㅉ", "/3jj/"},
        };
        bool tense_mapped = true;
        for (const HangulCase& row : tense)
            tense_mapped = tense_mapped && transform_hangul(row.plain) == row.encoded;
        check(tense_mapped, "tense Hangul consonants use literal doubling");

        static const HangulCase compound_vowels[] = {
            {"ㅐ", "/3ai/"}, {"ㅔ", "/3eoi/"}, {"ㅒ", "/3yai/"},
            {"ㅖ", "/3yeoi/"}, {"ㅘ", "/3oa/"}, {"ㅙ", "/3oai/"},
            {"ㅚ", "/3oi/"}, {"ㅝ", "/3ueo/"}, {"ㅞ", "/3ueoi/"},
            {"ㅟ", "/3ui/"}, {"ㅢ", "/3eui/"},
        };
        bool compound_vowels_mapped = true;
        for (const HangulCase& row : compound_vowels)
            compound_vowels_mapped = compound_vowels_mapped &&
                transform_hangul(row.plain) == row.encoded;
        check(compound_vowels_mapped, "compound Hangul vowels decompose into primitive codes");

        static const HangulCase compound_finals[] = {
            {"ㄳ", "/3gs/"}, {"ㄵ", "/3nj/"}, {"ㄶ", "/3nh/"},
            {"ㄺ", "/3lg/"}, {"ㄻ", "/3lm/"}, {"ㄼ", "/3lb/"},
            {"ㄽ", "/3ls/"}, {"ㄾ", "/3lt/"}, {"ㄿ", "/3lp/"},
            {"ㅀ", "/3lh/"}, {"ㅄ", "/3bs/"},
        };
        bool compound_finals_mapped = true;
        for (const HangulCase& row : compound_finals)
            compound_finals_mapped = compound_finals_mapped &&
                transform_hangul(row.plain) == row.encoded;
        check(compound_finals_mapped,
              "compound Hangul finals decompose into primitive consonant codes");

        check(transform_hangul("가") == "/1g0a0/" &&
                  transform_hangul("각") == "/1g0a0g/" &&
                  untransform_hangul("/1g0a0g/") == "각",
              "Hangul syllables preserve initial medial and optional final fields");
        check(transform_hangul("각가") == "/1g0a0g//1g0a0/" &&
                  transform_hangul("가까") == "/1g0a0//1gg0a0/" &&
                  transform_hangul("각가") != transform_hangul("가까") &&
                  untransform_hangul(transform_hangul("각가")) == "각가" &&
                  untransform_hangul(transform_hangul("가까")) == "가까",
              "Hangul syllable frames separate final consonants from tense initials");
        check(transform_hangul("받다") != transform_hangul("바따") &&
                  transform_hangul("갑바") != transform_hangul("가빠") &&
                  transform_hangul("갓사") != transform_hangul("가싸") &&
                  transform_hangul("갖자") != transform_hangul("가짜"),
              "all valid doubled consonant collision patterns remain distinct");

        const std::string nasty = "괜찮아요 값싼 얼룩을 찾고 있어요.";
        check(untransform_hangul(transform_hangul(nasty)) == nasty,
              "compound and adjacent Hangul syllables round trip with word spaces");
        const std::string decomposed = "각";
        check(transform_hangul(decomposed) == "/2g0a0g/" &&
                  untransform_hangul(transform_hangul(decomposed)) == decomposed,
              "valid decomposed modern Hangul jamo preserve their input form");
        check(validate_hangul_input("ᄀ").status ==
                  TransformValidationStatus::UnsupportedInput,
              "incomplete canonical jamo is rejected without guessing");
        check(validate_hangul_transformed_data("gagga").status ==
                  TransformValidationStatus::MalformedData,
              "flattened Hangul data is rejected without guessing syllable boundaries");
        check(transform_hangul("한 글 42.").find(' ') != std::string::npos &&
                  untransform_hangul(transform_hangul("한 글 42.")) == "한 글 42.",
              "Hangul word spaces digits and sentence marks remain reversible");
        check(is_supported_language("kor"), "Hangul is available through language selection");
        check(transform("Latin") == "l0atin" &&
                  untransform(transform("Latin 42")) == "Latin 42",
              "existing Latin behavior remains unchanged after Hangul support");
    }

    {
        const std::string valid = "A\xC3\xA9\xED\x95\x9C";
        const auto ascii = decode_utf8(valid, 0);
        const auto accent = decode_utf8(valid, 1);
        const auto hangul = decode_utf8(valid, 3);
        check(ascii.codepoint == 'A' && ascii.byte_width == 1 && !ascii.malformed &&
                  accent.codepoint == 0xE9 && accent.byte_width == 2 && !accent.malformed &&
                  hangul.codepoint == 0xD55C && hangul.byte_width == 3 && !hangul.malformed,
              "shared UTF-8 decoder reports codepoints and byte widths");
        const std::string malformed = "A\xF0\x28\x8C\x28";
        check(decode_utf8(malformed, 1).malformed && decode_utf8(malformed, 1).byte_width == 1 &&
                  decode_utf8(malformed, 3).malformed &&
                  validate_transform_input(malformed).status == TransformValidationStatus::InvalidUtf8 &&
                  validate_transform_input(malformed).offset == 1,
              "strict UTF-8 adapter retains the first malformed byte offset");
        check(decode_utf8("\xC0\xAF", 0).malformed &&
                  decode_utf8("\xED\xA0\x80", 0).malformed &&
                  decode_utf8("\xF4\x90\x80\x80", 0).malformed &&
                  decode_utf8("\xE2\x82", 0).malformed,
              "shared UTF-8 decoder rejects overlong surrogate range and truncated input");
    }
    {
        Settings s;
        s.suite_code = "38";
        s.rotors = {"R3", "R1", "R8", "R5", "R10"};
        s.reflector = "G";
        s.rings = {7, 19, 2, 33, 11};
        s.notches = {"a", "5", "#", "z", "/"};
        s.plugs = {"qw", "12"};
        s.master_key = "h4t#0p";
        s.marker = "abcdefghijklmnop";
        Machine machine = build_factory_demo_machine(s);
        PipelineConfig cfg;
        cfg.marker = s.marker;
        Pipeline pipe(machine, cfg);
        const std::string readable = "Cafe \xC3\xA9 42";
        const std::string folded = prepare_cli_text(readable, CliTextProfile::Latin);
        const Encrypted first = pipe.encrypt(folded);
        const std::string check_raw = pipe.decrypt(first.ciphertext);
        const std::string decrypt_human = restore_cli_text(check_raw, CliTextProfile::Latin);
        const std::string check_human = restore_cli_text(check_raw, CliTextProfile::Latin);
        const Encrypted second = pipe.encrypt(folded);
        const std::string batch_human = restore_cli_text(pipe.decrypt(second.ciphertext),
                                                          CliTextProfile::Latin);
        check(check_raw == folded && decrypt_human == readable &&
                  check_human == decrypt_human && batch_human == decrypt_human,
              "interactive decrypt encipher check and batch restore the same Latin profile");
        const auto decrypt_lines = cli_decrypt_lines(check_raw, CliTextProfile::Latin);
        const auto encipher_lines = cli_encipher_lines(check_raw, CliTextProfile::Latin);
        const auto batch_lines = cli_encipher_lines(pipe.decrypt(second.ciphertext),
                                                     CliTextProfile::Latin);
        check(decrypt_lines.size() == 2 && decrypt_lines[0].label == "plain  " &&
                  decrypt_lines[0].value == folded &&
                  decrypt_lines[1].label == "human  " &&
                  decrypt_lines[1].value == readable,
              "interactive decrypt output keeps plain then human labels and values");
        check(encipher_lines.size() == 2 && encipher_lines[0].label == "check  " &&
                  encipher_lines[0].value == folded &&
                  encipher_lines[1].label == "human  " &&
                  encipher_lines[1].value == readable &&
                  batch_lines[0].label == encipher_lines[0].label &&
                  batch_lines[0].value == encipher_lines[0].value &&
                  batch_lines[1].value == encipher_lines[1].value,
              "interactive encipher and batch output share check then human rules");
        check(prepare_cli_text(readable, CliTextProfile::Raw) == readable &&
                  restore_cli_text(folded, CliTextProfile::Raw) == folded &&
                  cli_decrypt_lines(folded, CliTextProfile::Raw).size() == 1 &&
                  cli_decrypt_lines(folded, CliTextProfile::Raw)[0].label == "plain  " &&
                  cli_encipher_lines(folded, CliTextProfile::Raw).size() == 1 &&
                  cli_encipher_lines(folded, CliTextProfile::Raw)[0].label == "check  ",
              "raw CLI profile keeps text unchanged and omits human output");
    }

    // 11. One test per guard in DESIGN section 6. The governing rule is
    //     that for every "do not remove" there must be a check that fails
    //     when it is removed, and every check below has been verified by
    //     deleting or inverting the thing it protects and watching it fail
    //     by name. verify_legacy_integrity() was already the right shape;
    //     nothing else copied it until now.
    {
        const Suite& s38 = suite("38");
        Alphabet a38(s38.alphabet);
        const std::string scratch = "inop_selftest_scratch.json";

        // G1. The entropy check runs BEFORE generation, not after. A batch
        //     drawn from a dead source looks exactly like a good one, so
        //     checking afterwards is checking nothing. Observed through a
        //     counter rather than a mock: if the call is deleted from
        //     build_wheel_batch(), this check fails.
        unsigned long before = entropy_check_count();
        WheelBatch good = build_wheel_batch(s38, true, 10, 900, 2);
        WheelBatch good_reflectors = build_wheel_batch(s38, false, 2, 900, 0);
        check(entropy_check_count() > before,
              "entropy_self_check runs before wheel generation");
        check(wheel_batch_problem(good, s38).empty(),
              "a freshly generated batch passes its own validation");
        std::string setup_error;
        const bool wrote_setup_pool =
            write_wheel_batch(scratch, good, s38, false, &setup_error) &&
            write_wheel_batch(scratch, good_reflectors, s38, true, &setup_error);
        const int setup_pool_loaded = wrote_setup_pool ? load_wheel_file(scratch) : 0;
        const std::vector<std::string> setup_rotor_pool = available_rotors(s38);
        const std::vector<std::string> setup_reflector_pool = available_reflectors(s38);
        check(setup_pool_loaded == 12 &&
                  std::find(setup_rotor_pool.begin(), setup_rotor_pool.end(), "U900") !=
                      setup_rotor_pool.end() &&
                  std::find(setup_reflector_pool.begin(), setup_reflector_pool.end(),
                            "K900") != setup_reflector_pool.end(),
              "a committed catalogue refreshes both eligible selection caches");
        bool factory_selection_hidden = true;
        for (const std::string& name : s38.rotor_names)
            factory_selection_hidden = factory_selection_hidden &&
                std::find(setup_rotor_pool.begin(), setup_rotor_pool.end(), name) ==
                    setup_rotor_pool.end();
        for (const std::string& name : s38.reflector_names)
            factory_selection_hidden = factory_selection_hidden &&
                std::find(setup_reflector_pool.begin(), setup_reflector_pool.end(), name) ==
                    setup_reflector_pool.end();
        check(factory_selection_hidden,
              "factory demonstration wheels are absent from normal selection lists");
        before = entropy_check_count();
        MachineConfig setup_generated = random_setup_settings(s38);
        bool generated_eligible = normal_reflector_name_is_eligible(s38, setup_generated.reflector);
        for (const std::string& name : setup_generated.rotors)
            generated_eligible = generated_eligible && normal_rotor_name_is_eligible(s38, name);
        check(entropy_check_count() > before && setup_generated.rotors.size() >= 5 &&
                  generated_eligible,
              "GUI setup generation runs the entropy check and uses only eligible wheels");

        WheelBatch replacement = build_wheel_batch(s38, true, 10, 950, 2);
        WheelBatch replacement_reflectors =
            build_wheel_batch(s38, false, 2, 950, 0);
        const bool wrote_replacement =
            write_wheel_batch(scratch, replacement, s38, false, &setup_error) &&
            write_wheel_batch(scratch, replacement_reflectors, s38, true, &setup_error);
        const int replacement_loaded = wrote_replacement ? load_wheel_file(scratch) : 0;
        const std::vector<std::string> replacement_rotor_pool = available_rotors(s38);
        const std::vector<std::string> replacement_reflector_pool = available_reflectors(s38);
        check(replacement_loaded == 12 &&
                  std::find(replacement_rotor_pool.begin(), replacement_rotor_pool.end(),
                            "U900") == replacement_rotor_pool.end() &&
                  std::find(replacement_reflector_pool.begin(), replacement_reflector_pool.end(),
                            "K900") == replacement_reflector_pool.end() &&
                  std::find(replacement_rotor_pool.begin(), replacement_rotor_pool.end(),
                            "U950") != replacement_rotor_pool.end() &&
                  std::find(replacement_reflector_pool.begin(), replacement_reflector_pool.end(),
                            "K950") != replacement_reflector_pool.end(),
              "reloading one source replaces stale wheels and refreshes both caches");

        // G2. A batch whose wirings are not all distinct is refused. This
        //     is the guard that DESIGN section 6 asserted was working while
        //     it was not (register item 35).
        WheelBatch dup;
        dup.rotors = true;
        dup.wirings = {good.wirings[0], good.wirings[0]};
        dup.wheels = {good.wheels[0], good.wheels[0]};
        check(!wheel_batch_problem(dup, s38).empty(),
              "a batch with two identical wirings is refused");

        // G3. A pure rotation of the alphabet is a Caesar wheel. Refused at
        //     generation, so it never reaches disk to be refused on load.
        std::string rot38;
        for (size_t i = 1; i <= s38.alphabet.size(); ++i)
            rot38 += s38.alphabet[i % s38.alphabet.size()];
        WheelBatch caesar;
        caesar.rotors = true;
        caesar.wirings = {rot38};
        caesar.wheels = {GeneratedWheel{"SELFTESTROT", rot38, ""}};
        check(!wheel_batch_problem(caesar, s38).empty(),
              "a batch containing a pure rotation is refused at generation");

        const std::vector<std::string> before_rejected_rotors = available_rotors(s38);
        const std::vector<std::string> before_rejected_reflectors = available_reflectors(s38);
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << R"({"rotors":[{"name":"SELFTESTPHANTOM","wiring":")"
              << replacement.wirings[0]
              << R"("}],"reflectors":[{"name":"SELFTESTBADREFLECTOR","wiring":")"
              << s38.alphabet << R"("}]})" << "\n";
        }
        std::vector<std::string> rejected_problems;
        const int rejected_loaded = load_wheel_file(scratch, &rejected_problems);
        check(rejected_loaded == 0 && !rejected_problems.empty() &&
                  available_rotors(s38) == before_rejected_rotors &&
                  available_reflectors(s38) == before_rejected_reflectors,
              "a rejected mixed catalogue leaves the live registry and caches unchanged");

        // G4. Nothing is written before validation, append case: the target
        //     must be byte-identical after a refusal.
        const std::string sentinel = "# sentinel, must survive a refused batch\n";
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << sentinel;
        }
        auto slurp = [](const std::string& p) {
            std::ifstream f(p, std::ios::binary);
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        };
        // Snapshot the bytes as they actually landed rather than comparing
        // against the string that was written: an ofstream in text mode
        // translates newlines on Windows, and a byte-identical check that
        // trips over that is testing the platform, not the guard.
        const std::string baseline = slurp(scratch);
        std::string err;
        check(!write_wheel_batch(scratch, dup, s38, /*append=*/true, &err),
              "write_wheel_batch refuses an invalid batch in append mode");
        check(slurp(scratch) == baseline,
              "append refusal leaves the existing file byte-identical");

        // G5. Overwrite is the worse case and the one that was not filed:
        //     std::ios::trunc empties the target at open, so validating
        //     after opening destroys the good wheels being replaced.
        check(!write_wheel_batch(scratch, dup, s38, /*append=*/false, &err),
              "write_wheel_batch refuses an invalid batch in overwrite mode");
        check(slurp(scratch) == baseline,
              "overwrite refusal leaves the existing file byte-identical");

        // G6. Positive control. Without this, G4 and G5 would still pass if
        //     write_wheel_batch refused everything unconditionally.
        check(write_wheel_batch(scratch, good, s38, /*append=*/false, &err),
              "write_wheel_batch does write a valid batch");
        check(slurp(scratch) != baseline && slurp(scratch).find(good.wirings[0]) != std::string::npos,
              "the written file actually contains the batch");
        std::vector<std::string> check_problems;
        check(validate_wheel_file(scratch, &check_problems) && check_problems.empty(),
              "read-only wheel check accepts a valid generated catalogue");
        const std::string first_serialization = slurp(scratch);
        check(write_wheel_batch(scratch, good, s38, /*append=*/false, &err) &&
                  slurp(scratch) == first_serialization,
              "the same wheel batch serializes reproducibly");
        int next_rotor = 0;
        std::string numbering_error;
        check(std::string(canonical_wheel_prefix(true)) == "U" &&
                  std::string(canonical_wheel_prefix(false)) == "K" &&
                  good.wheels.front().name == "U900" &&
                  good_reflectors.wheels.front().name == "K900",
              "wheel generation uses the canonical U and K identifiers");
        check(canonical_wheel_start(scratch, true, true, 3, &next_rotor, &numbering_error) &&
                  next_rotor == 910,
              "canonical append numbering continues after the highest existing identifier");
        check(!write_wheel_batch(scratch, good, s38, /*append=*/true, &err) &&
                  slurp(scratch) == first_serialization,
              "append rejects duplicate wheel IDs without changing the catalogue");

        WheelBatch historic = good;
        for (size_t i = 0; i < historic.wheels.size(); ++i)
            historic.wheels[i].name = "ARCHIVE" + std::to_string(i + 1);
        int first_canonical = 0;
        WheelBatch canonical_append = build_wheel_batch(s38, true, 2, 1, 2);
        const bool kept_historic =
            write_wheel_batch(scratch, historic, s38, false, &err) &&
            canonical_wheel_start(scratch, true, true, 2, &first_canonical, &numbering_error) &&
            first_canonical == 1 &&
            write_wheel_batch(scratch, canonical_append, s38, true, &err);
        check(kept_historic && slurp(scratch).find("ARCHIVE1") != std::string::npos &&
                  slurp(scratch).find("\"U1\"") != std::string::npos,
              "canonical append preserves existing noncanonical identifiers without renaming");

        WheelBatch bad_id = good;
        bad_id.wheels[0].name.clear();
        check(!wheel_batch_problem(bad_id, s38).empty(),
              "generated wheel batches require every ID");
        WheelBatch bad_notch = good;
        bad_notch.wheels[0].notches = "aa";
        check(!wheel_batch_problem(bad_notch, s38).empty(),
              "generated wheel batches reject duplicate notch symbols");

        // G7. The wheel file is validated on LOAD, not only on generation,
        //     so a bad file left on disk cannot poison a later session.
        //     Written as JSON by hand rather than through write_wheel_batch,
        //     because write_wheel_batch refuses both of these at generation:
        //     the point is to prove the load path checks them too.
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << R"({"rotors":[{"name":"SELFTESTD1","wiring":")" << good.wirings[0]
              << R"("},{"name":"SELFTESTD2","wiring":")" << good.wirings[0] << R"("}]})"
              << "\n";
        }
        std::vector<std::string> problems;
        check(load_wheel_file(scratch, &problems) == 0 && !problems.empty(),
              "load_wheel_file rejects a file whose rotors share a wiring");
        problems.clear();
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << R"({"rotors":[{"name":"SELFTESTROT","wiring":")" << rot38 << R"("}]})" << "\n";
        }
        check(load_wheel_file(scratch, &problems) == 0 && !problems.empty(),
              "load_wheel_file rejects a file containing a rotation");

        // G7b. A file that is not JSON at all is refused rather than
        //      silently loading nothing, so a 2.2.x wheel file left in
        //      place under its new name cannot pass as an empty pool.
        problems.clear();
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << "rotor SELFTESTOLD " << good.wirings[0] << "\n";
        }
        check(load_wheel_file(scratch, &problems) == 0 && !problems.empty(),
              "load_wheel_file rejects a file that is not JSON");
        problems.clear();
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << R"({"reflectors":[{"name":"SELFTESTFIXED","wiring":")"
              << s38.alphabet << R"("}]})" << "\n";
        }
        check(load_wheel_file(scratch, &problems) == 0 && !problems.empty(),
              "load_wheel_file rejects a reflector with fixed points");
        problems.clear();
        {
            std::ofstream f(scratch, std::ios::trunc);
            f << R"({"reflectors":[{"name":"SELFTESTNONINV","wiring":")"
              << rot38 << R"("}]})" << "\n";
        }
        check(load_wheel_file(scratch, &problems) == 0 && !problems.empty(),
              "load_wheel_file rejects a reflector that is not an involution");
        problems.clear();
        check(!validate_wheel_document(
                  R"({"rotors":[{"name":"DUP","wiring":"abcdefghijklmnopqrstuvwxyz0123456789#/"},{"name":"DUP","wiring":"abcdefghijklmnopqrstuvwxyz0123456789#/"}]})",
                  &problems) && !problems.empty(),
              "wheel validation rejects duplicate IDs before map insertion");
        problems.clear();
        check(!validate_wheel_document(R"({"rotors":[{"wiring":"missing-id"}]})", &problems) &&
                  !problems.empty(),
              "wheel validation rejects entries with missing IDs");
        problems.clear();
        check(!validate_wheel_document(R"({"rotors":[7]})", &problems) && !problems.empty(),
              "wheel validation rejects malformed catalogue entries");
        problems.clear();
        check(!validate_wheel_document("{}", &problems) && !problems.empty(),
              "wheel validation rejects a missing catalogue");
        std::remove(scratch.c_str());

        // G8. random_notches() clamps to a floor of one. A notch-less rotor
        //     never advances the rotor to its left, which collapses the
        //     period exactly as a fixed rotor would.
        check(random_notches(a38, 0).size() == 1, "random_notches(0) still yields one notch");
        check(random_notches(a38, -3).size() == 1, "random_notches(-3) still yields one notch");

        // G9. apply_suite_lock() forces the Legacy restrictions off. Also
        //     covered indirectly by verify_legacy_integrity(); stated here
        //     directly so deleting one line of it is visible.
        {
            PipelineConfig cfg;
            cfg.double_pass = true;
            cfg.padding = true;
            cfg.moving_reflector = true;
            bool locked = apply_suite_lock(cfg, /*historic_lock=*/true, 5);
            check(locked && !cfg.double_pass && !cfg.padding && !cfg.moving_reflector,
                  "apply_suite_lock forces double pass, padding and moving reflector off");
            PipelineConfig open_cfg;
            open_cfg.double_pass = true;
            open_cfg.padding = true;
            open_cfg.moving_reflector = true;
            bool unlocked = apply_suite_lock(open_cfg, /*historic_lock=*/false, 16);
            check(!unlocked && open_cfg.double_pass && open_cfg.padding &&
                      open_cfg.moving_reflector,
                  "apply_suite_lock leaves a non-historic suite alone");
        }

        // G10. The notch rule has one home. kMaxNotchesAnySuite is a
        //      compile-time constant an interface can size an array with,
        //      so the suite table is checked against it here rather than
        //      trusted to stay in step. It did not stay in step once: the
        //      GUI carried three notch boxes for a cap that had moved to
        //      five, and silently truncated generated rotors to fit.
        {
            int widest = 0;
            for (const auto& [code, su] : suites())
                if (su.max_notches > widest) widest = su.max_notches;
            check(widest == kMaxNotchesAnySuite,
                  "kMaxNotchesAnySuite (" + std::to_string(kMaxNotchesAnySuite) +
                      ") equals the widest suite max_notches (" + std::to_string(widest) + ")");
            check(duplicate_notch_symbols({"abc", "def"}).empty(),
                  "distinct notches across rotors are accepted");
            check(duplicate_notch_symbols({"abc", "cde"}) == "c",
                  "a notch symbol shared by two rotors is reported");
            check(duplicate_notch_symbols({"aa"}) == "a",
                  "a notch symbol repeated within one rotor is reported");
            check(duplicate_notch_symbols({"abc", "cda", "e"}) == "ac",
                  "every clashing symbol is reported, not just the first");
        }

        // G11. Key material must not be tracked by git. A ratchet, not a
        //      remedy: nothing is tracked today and this is what keeps it
        //      that way.
        std::vector<std::string> tracked = tracked_key_material();
        check(tracked.empty(),
              tracked.empty() ? "no key material is tracked by git"
                              : "KEY MATERIAL IS TRACKED BY GIT: " + tracked.front());
    }

    // 12. The interface layer. Everything above this point is the logic
    //     layer, which was the whole of the suite until now. These are the
    //     pieces sitting between that logic and a terminal or a window,
    //     and they were covered by nothing.
    {
        // Batch splitting. A message may span lines; a blank line, or a
        // line holding nothing but spaces, is what ends one.
        check(split_batch_messages("").empty(), "empty batch input gives no messages");

        std::vector<std::string> two = split_batch_messages("first\n\nsecond\n");
        check(two.size() == 2 && two[0] == "first" && two[1] == "second",
              "a blank line separates two batch messages");

        std::vector<std::string> joined = split_batch_messages("line one\nline two\n\nnext\n");
        check(joined.size() == 2 && joined[0] == "line one line two" && joined[1] == "next",
              "a batch message spanning lines is joined with a space");

        std::vector<std::string> padded = split_batch_messages("\n\n\nonly\n\n\n");
        check(padded.size() == 1 && padded[0] == "only",
              "blank runs at either end produce no empty batch messages");

        std::vector<std::string> spaced = split_batch_messages("one\n   \ntwo\n");
        check(spaced.size() == 2, "a line of nothing but spaces ends a batch message");
    }
    {
        const std::string path = "inop_selftest_batch.txt";
        { std::ofstream f(path, std::ios::binary); f << "one\n\ntwo\n"; }
        std::string raw, err;
        const bool ok = read_batch_file(path, raw, &err);
        std::remove(path.c_str());
        check(ok && raw == "one\n\ntwo\n", "a batch file reads back whole");

        std::string gone, gone_err;
        check(!read_batch_file("inop_selftest_no_batch.txt", gone, &gone_err) && !gone_err.empty(),
              "a batch file that is not there is refused, with a reason");
    }
    {
        // The cap is answered from the file size before a byte is read, so
        // an oversized file can never be half processed.
        const std::string path = "inop_selftest_big.txt";
        {
            std::ofstream f(path, std::ios::binary);
            f << std::string(MAX_BATCH_FILE_BYTES + 1, 'a');
        }
        std::string raw, err;
        const bool ok = read_batch_file(path, raw, &err);
        std::remove(path.c_str());
        check(!ok && !err.empty() && raw.empty(),
              "a batch file over the floppy cap is refused before it is read");
    }
    {
        Settings valid;
        valid.suite_code = "38";
        valid.rotors = {"U950", "U951", "U952", "U953", "U954"};
        valid.reflector = "K950";
        valid.rings = {1, 2, 3, 4, 5};
        valid.notches = {"a", "b", "c", "d", "e"};
        valid.plugs = {"fg"};
        valid.master_key = "abcde0";
        valid.marker = "abcdefghijklmnop";
        std::string err;
        check(validate_settings(valid, &err), "complete valid settings are accepted");
        auto direct_from_config = [](const MachineConfig& config) {
            const Suite& selected = suite(config.suite_code);
            Alphabet alpha(selected.alphabet);
            std::vector<Rotor> wheels;
            for (size_t i = 0; i < config.rotors.size(); ++i) {
                Rotor wheel = make_rotor(config.rotors[i], alpha);
                if (!selected.notches_are_fixed) wheel.set_notches(config.notches[i], alpha);
                wheels.push_back(std::move(wheel));
            }
            std::string key = config.master_key;
            if (selected.historic_lock) {
                key.resize(config.rotors.size());
                key += alpha.at(0);
            }
            return Machine(alpha, std::move(wheels), make_reflector(config.reflector, alpha),
                           Plugboard(config.plugs, alpha), config.rings, key, selected.historic_lock);
        };
        Machine direct = direct_from_config(valid);
        Machine canonical = build_machine(valid);
        check(direct.encipher("abcdefghijklmnop") == canonical.encipher("abcdefghijklmnop"),
              "normal builder matches explicit rotor construction");

        Settings changed = valid;
        changed.rings[0] = 0;
        check(!validate_settings(changed, &err), "ring zero is rejected before machine construction");
        changed = valid;
        changed.rotors[1] = changed.rotors[0];
        check(!validate_settings(changed, &err), "duplicate rotors are rejected before machine construction");
        changed = valid;
        changed.rotors[0] = "MISSING";
        check(!validate_settings(changed, &err), "unavailable rotors are rejected before machine construction");
        changed = valid;
        changed.reflector = "MISSING";
        check(!validate_settings(changed, &err), "unavailable reflectors are rejected before machine construction");
        changed = valid;
        changed.rotors[0] = "R1";
        check(!validate_settings(changed, &err) && err.find("factory demonstration rotor") != std::string::npos,
              "saved settings reject an excluded factory rotor with a clear reason");
        changed = valid;
        changed.reflector = "D";
        check(!validate_settings(changed, &err) &&
                  err.find("factory demonstration reflector") != std::string::npos,
              "saved settings reject an excluded factory reflector with a clear reason");
        changed = valid;
        changed.notches[1] = "a";
        check(!validate_settings(changed, &err), "repeated notch symbols are rejected before machine construction");
        changed = valid;
        changed.master_key.back() = '!';
        check(!validate_settings(changed, &err), "master key symbols outside the alphabet are rejected");

        auto rejected = [&](const std::string& name, auto edit, MachineField field,
                            const std::string& expected) {
            Settings candidate = valid;
            edit(candidate);
            const MachineValidation result = validate_machine_config(candidate);
            std::string legacy_error;
            const bool legacy_valid = validate_settings(candidate, &legacy_error);
            check(!result.valid && !result.diagnostics.empty() && !legacy_valid &&
                      result.diagnostics.front().field == field &&
                      result.diagnostics.front().message == expected && legacy_error == expected,
                  "configuration equivalence: " + name);
        };
        rejected("no rotors", [](Settings& s) { s.rotors.clear(); },
                 MachineField::RotorCount, "no rotors listed");
        rejected("unknown suite", [](Settings& s) { s.suite_code = "missing"; },
                 MachineField::Suite, "unknown suite 'missing'");
        rejected("rotor count", [](Settings& s) { s.rotors.pop_back(); },
                 MachineField::RotorCount, "rotor count 4 is outside INOP-38's range 5-10");
        rejected("unavailable rotor", [](Settings& s) { s.rotors[0] = "MISSING"; },
                 MachineField::Rotor, "rotor MISSING is not available for INOP-38");
        rejected("duplicate rotor", [](Settings& s) { s.rotors[1] = s.rotors[0]; },
                 MachineField::Rotor, "rotor U950 is used more than once");
        rejected("factory rotor", [](Settings& s) { s.rotors[0] = "R1"; },
                 MachineField::Rotor, "factory demonstration rotor R1 is excluded from normal INOP-38 configurations");
        rejected("unavailable reflector", [](Settings& s) { s.reflector = "MISSING"; },
                 MachineField::Reflector, "reflector MISSING is not available for INOP-38");
        rejected("factory reflector", [](Settings& s) { s.reflector = "D"; },
                 MachineField::Reflector, "factory demonstration reflector D is excluded from normal INOP-38 configurations");
        rejected("ring count", [](Settings& s) { s.rings.pop_back(); },
                 MachineField::Ring, "rings count (4) does not match rotor count (5)");
        rejected("ring range", [](Settings& s) { s.rings[0] = 0; },
                 MachineField::Ring, "ring value 0 is outside 1-38");
        rejected("ring upper range", [](Settings& s) { s.rings[0] = 39; },
                 MachineField::Ring, "ring value 39 is outside 1-38");
        rejected("notch count", [](Settings& s) { s.notches.pop_back(); },
                 MachineField::Notch, "notches count (4) does not match rotor count (5)");
        rejected("empty notch", [](Settings& s) { s.notches[0].clear(); },
                 MachineField::Notch, "each rotor needs 1-5 notch symbols");
        rejected("notch limit", [](Settings& s) { s.notches[0] = "fghijk"; },
                 MachineField::Notch, "each rotor needs 1-5 notch symbols");
        rejected("notch alphabet", [](Settings& s) { s.notches[0] = "!"; },
                 MachineField::Notch, "notch symbol is outside the INOP-38 alphabet");
        rejected("duplicate notch", [](Settings& s) { s.notches[1] = "a"; },
                 MachineField::Notch, "notch symbols are repeated across rotors");
        rejected("plug count", [](Settings& s) { s.plugs.assign(16, "fg"); },
                 MachineField::Plugboard, "plugboard pair count exceeds 15");
        rejected("plug shape", [](Settings& s) { s.plugs = {"f"}; },
                 MachineField::Plugboard, "plugboard pair 'f' must be exactly 2 symbols");
        rejected("plug self link", [](Settings& s) { s.plugs = {"ff"}; },
                 MachineField::Plugboard, "plugboard cannot connect a symbol to itself");
        rejected("plug alphabet", [](Settings& s) { s.plugs = {"f!"}; },
                 MachineField::Plugboard, "plugboard pair 'f!' uses a symbol outside the alphabet");
        rejected("plug reuse", [](Settings& s) { s.plugs = {"fg", "fh"}; },
                 MachineField::Plugboard, "plugboard pair 'fh' reuses an already-patched symbol");
        rejected("key length", [](Settings& s) { s.master_key.pop_back(); },
                 MachineField::MasterKey, "key length (5) does not match rotor count (5)");
        rejected("key alphabet", [](Settings& s) { s.master_key.back() = '!'; },
                 MachineField::MasterKey, "master key symbol is outside the INOP-38 alphabet");
        rejected("marker length", [](Settings& s) { s.marker.pop_back(); },
                 MachineField::Marker, "marker must contain exactly 16 INOP-38 alphabet symbols");
        rejected("marker alphabet", [](Settings& s) { s.marker.back() = '!'; },
                 MachineField::Marker, "marker must contain exactly 16 INOP-38 alphabet symbols");
        Settings multiple = valid;
        multiple.rings[0] = 0;
        multiple.marker.clear();
        const MachineValidation multi_result = validate_machine_config(multiple);
        check(multi_result.diagnostics.size() == 2 &&
                  multi_result.diagnostics[0].field == MachineField::Ring &&
                  multi_result.diagnostics[0].index == 0 &&
                  multi_result.diagnostics[1].field == MachineField::Marker,
              "structured validation reports independent field diagnostics");
        Settings legacy;
        legacy.suite_code = "26";
        legacy.rotors = {"I", "II", "III"};
        legacy.reflector = "B";
        legacy.rings = {1, 1, 1};
        legacy.master_key = "AAA";
        check(validate_machine_config(legacy).valid && validate_settings(legacy, &err),
              "Legacy base key stays accepted by both validation entry points");
        legacy.notches = {"ignored"};
        check(validate_machine_config(legacy).valid && validate_settings(legacy, &err),
              "Legacy fixed notches keep the prior permissive field behavior");
        legacy.notches.clear();
        legacy.master_key = "AAAZ";
        check(validate_machine_config(legacy).valid && validate_settings(legacy, &err),
              "Legacy extra reflector symbol stays accepted by both validation entry points");
        Machine legacy_direct = direct_from_config(legacy);
        Machine legacy_canonical = build_machine(legacy);
        check(legacy_direct.encipher("AAAAAAAA") == legacy_canonical.encipher("AAAAAAAA"),
              "Legacy builder matches explicit rotor construction");
        legacy.master_key = "AAA";
        legacy.marker = "A";
        const MachineValidation legacy_marker = validate_machine_config(legacy);
        check(!legacy_marker.valid && legacy_marker.diagnostics.front().field == MachineField::Marker &&
                  legacy_marker.diagnostics.front().message == "Legacy settings must not contain a marker",
              "Legacy marker remains rejected with its prior error");
        MachineConfig maximum = random_settings(suite("38"), 10, 15, 3);
        check(validate_machine_config(maximum).valid && validate_settings(maximum, &err),
              "maximum normal configuration stays accepted by both validation entry points");
    }
    {
        const std::string path = "inop_selftest_bad_settings.json";
        {
            std::ofstream f(path, std::ios::binary);
            f << R"({"suite_code":"38","reflector":"D","master_key":"abcde0","rotors":[{"name":"R1","ring":"1x","notches":"a"}],"plugboard":[]})";
        }
        Settings unchanged;
        unchanged.suite_code = "sentinel";
        std::string err;
        const bool loaded = load_settings(unchanged, path, &err);
        std::remove(path.c_str());
        check(!loaded && unchanged.suite_code == "sentinel" && !err.empty(),
              "malformed JSON ring is rejected without changing active settings");
    }
    {
        const std::string setup =
            R"({"suite_code":"38","reflector":"K950","master_key":"abcde0","rotor_count":5,"rotors":[{"name":"U950","ring":"1","notches":"a"},{"name":"U951","ring":"2","notches":"b"},{"name":"U952","ring":"3","notches":"c"},{"name":"U953","ring":"4","notches":"d"},{"name":"U954","ring":"5","notches":"e"}],"plugboard":[]})";
        const std::string path = "inop_selftest_old_settings.json";
        { std::ofstream f(path, std::ios::binary); f << setup; }
        Settings old;
        std::string err;
        bool marker_missing = false;
        const bool loaded = load_settings(old, path, &err, &marker_missing);
        std::remove(path.c_str());
        old.marker = "abcdefghijklmnop";
        check(loaded && marker_missing && validate_settings(old, &err),
              "older settings require explicit marker completion before use");

        const std::string sheet_path = "inop_selftest_old_sheet.json";
        { std::ofstream f(sheet_path, std::ios::binary); f << "{\"entries\":[" << setup << "]}"; }
        Settings sheet_entry;
        bool sheet_marker_missing = false;
        const bool sheet_loaded =
            load_keysheet_entry(sheet_path, 1, sheet_entry, &err, &sheet_marker_missing);
        std::remove(sheet_path.c_str());
        check(sheet_loaded && sheet_marker_missing && sheet_entry.marker.empty(),
              "older key sheet entry imports incomplete without a hidden marker");
    }
    {
        // A settings file has to come back as what went into it. Generated
        // rather than hand written, so this covers whatever a real
        // configuration carries rather than whatever was easy to type.
        const Suite& s38 = suite("38");
        MachineConfig g = random_settings(s38, 5, 3, 1);
        Settings wrote;
        wrote.suite_code = g.suite_code;
        wrote.rotors = g.rotors;
        wrote.reflector = g.reflector;
        wrote.rings = g.rings;
        wrote.notches = g.notches;
        wrote.plugs = g.plugs;
        wrote.master_key = g.master_key;
        wrote.marker = g.marker;

        const std::string path = "inop_selftest_settings.json";
        const bool saved = save_settings(wrote, path);
        Settings read;
        std::string err;
        const bool loaded = load_settings(read, path, &err);
        std::remove(path.c_str());
        check(saved && loaded && read.suite_code == wrote.suite_code &&
                  read.rotors == wrote.rotors && read.reflector == wrote.reflector &&
                  read.rings == wrote.rings && read.notches == wrote.notches &&
                  read.plugs == wrote.plugs && read.master_key == wrote.master_key &&
                  read.marker == wrote.marker,
              "a settings file survives a save and a load unchanged");
    }
    {
        // Key sheet indexing. Entry n has to be entry n, and an index past
        // the end has to be refused rather than clamped to the last one.
        const std::string path = "inop_selftest_sheet.json";
        const Suite& s38 = suite("38");
        std::string first, err;
        const bool wrote = write_key_sheet(path, s38, 3, 2, 1, false, 5, &first, &err);
        const int n = count_keysheet_entries(path);

        Settings one, three, past;
        std::string e1, e3, ep;
        const bool got1 = load_keysheet_entry(path, 1, one, &e1);
        const bool got3 = load_keysheet_entry(path, 3, three, &e3);
        const bool got_past = load_keysheet_entry(path, 4, past, &ep);
        std::vector<KeySheetEntry> loaded_entries;
        std::string sheet_error;
        const bool loaded_once = load_keysheet(path, loaded_entries, &sheet_error);
        std::remove(path.c_str());

        check(wrote && n == 3, "a written key sheet counts its own entries");
        check(got1 && got3 && one.master_key != three.master_key,
              "key sheet entry one and entry three are different entries");
        check(!got_past && !ep.empty(), "a key sheet index past the end is refused");
        check(loaded_once && loaded_entries.size() == 3 && loaded_entries[0].valid &&
                  loaded_entries[2].valid,
              "a complete key sheet is parsed and validated in one operation");
    }
    {
        // The 2.2.x plain text settings file has to survive the move to
        // JSON. An operator upgrading has one on disk and nothing else.
        const std::string txt = "inop_selftest_old.settings";
        const std::string json = "inop_selftest_new.json";
        const Suite& s38 = suite("38");
        MachineConfig g = random_settings(s38, 5, 2, 1);
        std::ostringstream text;
        text << "suite " << g.suite_code << "\nrotors";
        for (const auto& rotor : g.rotors) text << " " << rotor;
        text << "\nreflector " << g.reflector << "\nrings";
        for (int ring : g.rings) text << " " << ring;
        text << "\nnotches";
        for (const auto& notch : g.notches) text << " " << notch;
        text << "\nplugs";
        for (const auto& plug : g.plugs) text << " " << plug;
        text << "\nmarker " << g.marker << "\nkey " << g.master_key << "\n";
        std::string old_text = text.str();
        const size_t marker_line = old_text.find("marker ");
        if (marker_line != std::string::npos) {
            const size_t line_end = old_text.find('\n', marker_line);
            old_text.erase(marker_line, line_end == std::string::npos
                                            ? std::string::npos
                                            : line_end - marker_line + 1);
        }
        {
            std::ofstream f(txt, std::ios::binary);
            f << old_text;
        }

        const bool migrated = migrate_settings_from_text(txt, json);
        Settings read;
        std::string err;
        bool marker_missing = false;
        const bool loaded = migrated && load_settings(read, json, &err, &marker_missing);
        std::remove(txt.c_str());
        std::remove(json.c_str());
        check(migrated && loaded && marker_missing && read.marker.empty() &&
                  read.master_key == g.master_key &&
                  read.rotors == g.rotors && read.reflector == g.reflector,
              "an old plain text settings file migrates without inventing a marker");
    }

    {
        const auto& presets = developer_setup_presets();
        check(presets.size() == 3, "three fixed public benchmark setups are available");
        bool all_valid = presets.size() == 3;
        for (const auto& preset : presets)
            all_valid = all_valid && preset.settings.public_builtin_preset &&
                        validate_machine_config(preset.settings).valid;
        check(all_valid, "public benchmark setups validate independently of loaded wheels");
        const std::string sample = "abcdefghijklmnopqrstuvwxyz0123456789";
        Machine before = build_machine(presets[0].settings);
        const std::string baseline = before.encipher(sample);
        const std::string missing = "inop_selftest_missing_public_wheels.json";
        check(load_wheel_file(missing) == 0 &&
                  build_machine(presets[0].settings).encipher(sample) == baseline,
              "a missing wheel file does not affect public presets");
        const std::string scratch = "inop_selftest_public_collision.json";
        {
            std::ofstream file(scratch, std::ios::binary);
            file << R"({"rotors":[{"name":"R1","wiring":"1q27#cpzl3rhv6mktjuxfbe5o9n0as4di/ywg8","notches":""}],"reflectors":[{"name":"D","wiring":"rcbywiptfu#97x/g1a3hj5end26qzs8v0m4lko"}]})";
        }
        const int loaded = load_wheel_file(scratch);
        std::remove(scratch.c_str());
        const Alphabet alpha(ALPHA38);
        const bool shadow_active = make_rotor("R1", alpha).fwd_table()[0] !=
                                   make_builtin_rotor("R1", alpha).fwd_table()[0];
        check(loaded == 2 && shadow_active &&
                  build_machine(presets[0].settings).encipher(sample) == baseline,
              "loaded name collisions cannot change public benchmark wiring");
        MachineConfig ordinary = presets[0].settings;
        ordinary.public_builtin_preset = false;
        check(!validate_machine_config(ordinary).valid &&
                  !save_settings(presets[0].settings, "inop_selftest_forbidden_public.json"),
              "public setups cannot enter the normal settings path or be saved");
    }

    // 13. Whatever the GUI can be asked without opening a window. Silent
    //     in a build with no GUI compiled into it, because none of the
    //     files those checks cover are there to pass or fail.
    gui_self_test(check);

    rule();
    if (failures == 0) std::cout << GREEN << "all checks passed" << RST << "\n";
    else std::cout << RED << failures << " check(s) failed" << RST << "\n";
    return failures == 0 ? 0 : 1;
}
