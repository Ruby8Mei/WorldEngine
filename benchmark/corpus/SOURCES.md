# Corpus sources

`real_message` benchmark text for all 48 supported languages, pulled
2026-08-20 from Wikipedia via the MediaWiki `action=query&prop=extracts`
API (plain-text extracts, no wiki markup). All Wikipedia text is
CC BY-SA 4.0 — https://creativecommons.org/licenses/by-sa/4.0/ — attribution
per article below. Each file is trimmed to roughly 4000-5000 characters,
cut at a paragraph or sentence boundary.

Heuristic: each language's own Wikipedia edition's article about its
country/nation, since those are reliably well-developed even on small
Wikipedias. Fallback where the country article was too short: the
language's own "[Language] language" article instead.

Hebrew (`heb`) was dropped from supported languages entirely rather than
sourced — see the project roadmap for the planned non-Latin-native-script
phase this is deferred to.

## Latin-script, sourced as-is

| code | language | edition | article | note |
|------|----------|---------|---------|------|
| sqi | Albanian | sq.wikipedia.org | Shqipëria | |
| eus | Basque | eu.wikipedia.org | Euskal Herria | stateless nation, cultural-region article |
| bos | Bosnian | bs.wikipedia.org | Bosna i Hercegovina | |
| cat | Catalan | ca.wikipedia.org | Catalunya | |
| cpf | Creole | ht.wikipedia.org | Ayiti | cpf covers a whole ISO creole family; Haitian Creole picked per operator |
| hrv | Croatian | hr.wikipedia.org | Hrvatska | |
| czr | Czech | cs.wikipedia.org | Česko | |
| dan | Danish | da.wikipedia.org | Danmark | |
| nld | Dutch | nl.wikipedia.org | Nederland | |
| eng | English | en.wikipedia.org | United Kingdom | |
| est | Estonian | et.wikipedia.org | Eesti | |
| fin | Finnish | fi.wikipedia.org | Suomi | |
| fra | French | fr.wikipedia.org | France | |
| deu | German | de.wikipedia.org | Deutschland | |
| hun | Hungarian | hu.wikipedia.org | Magyarország | |
| ibo | Igbo | ig.wikipedia.org | Naijiria | |
| ind | Indonesian | id.wikipedia.org | Indonesia | |
| gle | Irish | ga.wikipedia.org | Éire | |
| ita | Italian | it.wikipedia.org | Italia | |
| kmr | Kurdish (Kurmanji) | ku.wikipedia.org | Kurdistan | ku. edition is Kurmanji-specific since the 2009 Sorani (ckb.) split; no separate kmr. domain exists |
| lat | Latin | la.wikipedia.org | Lingua Latina | no living nation; language-article fallback used directly |
| lit | Lithuanian | lt.wikipedia.org | Lietuva | |
| ltz | Luxembourgish | lb.wikipedia.org | Lëtzebuergesch | country article "Lëtzebuerg" was a 471-char stub; language-article fallback used |
| mly | Malay | ms.wikipedia.org | Malaysia | |
| mlt | Maltese | mt.wikipedia.org | Malta | |
| mri | Maori | mi.wikipedia.org | Reo Māori | country article "Aotearoa" was a 2422-char stub; language-article fallback used |
| nor | Norwegian | no.wikipedia.org | Norge | Bokmål edition |
| pol | Polish | pl.wikipedia.org | Polska | |
| por | Portuguese | pt.wikipedia.org | Portugal | |
| ron | Romanian | ro.wikipedia.org | România | |
| gla | Scottish Gaelic | gd.wikipedia.org | Alba | |
| svk | Slovak | sk.wikipedia.org | Slovensko | |
| slv | Slovenian | sl.wikipedia.org | Slovenija | |
| som | Somali | so.wikipedia.org | Soomaaliya | |
| spa | Spanish | es.wikipedia.org | España | |
| swa | Swahili | sw.wikipedia.org | Tanzania | |
| swe | Swedish | sv.wikipedia.org | Sverige | |
| tgl | Tagalog | tl.wikipedia.org | Pilipinas | |
| tur | Turkish | tr.wikipedia.org | Türkiye | |
| cym | Welsh | cy.wikipedia.org | Cymru | |
| yor | Yoruba | yo.wikipedia.org | Nàìjíríà | |
| zul | Zulu/Xhosa | zu.wikipedia.org | IRiphabhuliki yaseNingizimu Afrika | Zulu picked of the two per app's combined code; no diacritics needed for either per decode table |

## Script-converted (mechanical, algorithmic transliteration)

| code | language | edition | article | conversion |
|------|----------|---------|---------|------------|
| cmn | Mandarin | zh.wikipedia.org | 中國 (redirected from 中国) | Hanyu Pinyin with tone marks via the `pypinyin` library (dictionary/phrase-based, not naive per-character) |
| hin | Hindi | hi.wikipedia.org | भारत | Devanagari → full academic IAST, script-written mechanical mapping (vowel signs, virama/conjuncts, anusvara/visarga, nukta letters) — verified against known words (भारत → bhārata, नमस्ते → namaste, हिन्दी → hindī, बड़ा → baṛā) before running on the full article |
| kor | Korean | ko.wikipedia.org | 대한민국 | Hangul → Revised Romanization (2000), per-syllable jamo decomposition — verified against known words (한국 → hanguk, 서울 → seoul, 대한민국 → daehanminguk). Does **not** apply RR's cross-syllable consonant-assimilation rules, just the direct per-syllable table; acceptable since the app's `kor` decode table is empty by design (RR avoids diacritics) |
| srp | Serbian | sr.wikipedia.org | Србија | Cyrillic → Latin (Gajica), standard 1:1/digraph mapping (љ→lj, њ→nj, џ→dž, etc.) |
| cnr | Montenegrin | *(none — see below)* | Црна Гора (sr.wikipedia.org) | **Adapted, not native-sourced** — Montenegrin has no live Wikipedia edition (only an unlaunched Incubator test wiki as of 2026-08-20). Sourced from Serbian Wikipedia's Montenegro article instead and run through the same Cyrillic→Latin conversion as `srp`. Does **not** apply Montenegrin's 2009 orthography ś/ź jat-reflex adjustments (e.g. sjediti→śediti) — that requires word-level phonological knowledge beyond a character mapping, so those two marks in the `cnr` decode table are not exercised by this corpus. Flagged to the operator; accepted as the best available option given no real source exists. |
| yue | Cantonese | zh-yue.wikipedia.org | 香港 (Hong Kong) | **Custom scheme, not Jyutping/Yale** — the app's `yue` decode table (see comment at languages.cpp:207) is devised for this project: the six Cantonese tones map onto this project's existing mark-shape digits (macron/acute/caron/grave/circumflex/tilde), one plain vowel per syllable carrying the mark, the same way Mandarin reuses four of them for its own tones. Source Han text dictionary-converted to Jyutping via the `pycantonese` library (word-segmented, not naive per-character), then each syllable's tone digit mapped onto the first vowel-ish letter (a/e/i/o/u, with `y` in "yu" finals treated as a stand-in for `u`) using the project's digit convention: tone 1→1(macron), 2→2(acute), 3→3(caron), 4→4(grave), 5→5(circumflex), 6→7(tilde, since 6 is already umlaut). Syllabic nasals (m/ng) have no vowel to mark and are left plain. Chinese punctuation is stripped to spaces *before* Jyutping segmentation — leaving it in confused the segmenter, which merged an unrecognized punctuation-plus-hanzi span into one unglossed token and silently dropped every syllable inside it (caught and fixed during conversion). All resulting diacritics verified to be exactly the 30 characters the `yue` decode table defines, nothing extra. |

## Not supported

Hebrew (`heb`) was dropped from the app's supported-language list entirely
on 2026-08-20, not just left without corpus — real-world Hebrew text has no
niqqud (vowel points), which the decode table's academic scheme
(ḥ ṭ ṣ š ś) needs to render meaningfully, and shin/sin (š/ś) can't be
disambiguated without it either. Deferred to whenever the project takes on
non-Latin-native scripts properly.
