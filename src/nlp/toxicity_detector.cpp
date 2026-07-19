#include "nlp/toxicity_detector.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <iostream>

namespace nlp {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string remove_non_alphanumeric(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (std::isalnum(c) || c == ' ') {
            result += c;
        }
    }
    return result;
}

} // namespace

ToxicityDetector::ToxicityDetector() {
    initialize_toxic_wordlist();
    index_words();
}

ToxicityDetector::~ToxicityDetector() = default;

void ToxicityDetector::initialize_toxic_wordlist() {
    toxic_words_ = {
        // Phrase-level patterns are checked before overlapping single words so
        // the audio muter can silence the complete expression as one range.
        {"i will kill you", ToxicityLevel::HIGH, "threat"},
        {"kill yourself", ToxicityLevel::HIGH, "self_harm"},
        {"shut the fuck up", ToxicityLevel::HIGH, "harassment"},
        {"go fuck yourself", ToxicityLevel::HIGH, "harassment"},
        {"what the fuck", ToxicityLevel::HIGH, "profanity"},
        {"go to hell", ToxicityLevel::HIGH, "harassment"},
        {"fuck off", ToxicityLevel::HIGH, "harassment"},
        {"shut up", ToxicityLevel::MEDIUM, "harassment"},
        {"fuck you", ToxicityLevel::HIGH, "insult"},
        {"screw you", ToxicityLevel::HIGH, "insult"},
        {"you suck", ToxicityLevel::MEDIUM, "insult"},
        {"i hate you", ToxicityLevel::MEDIUM, "insult"},
        {"piece of shit", ToxicityLevel::HIGH, "insult"},
        {"son of a bitch", ToxicityLevel::HIGH, "insult"},
        {"you are an idiot", ToxicityLevel::HIGH, "insult"},
        {"you are stupid", ToxicityLevel::HIGH, "insult"},
        {"stupid idiot", ToxicityLevel::HIGH, "insult"},

        // High toxicity - severe slurs and hate speech (based on research on online toxicity)
        {"damn", ToxicityLevel::LOW, "profanity"},
        {"hell", ToxicityLevel::LOW, "profanity"},
        {"crap", ToxicityLevel::MEDIUM, "profanity"},
        {"fuck", ToxicityLevel::HIGH, "profanity"},
        {"fucked", ToxicityLevel::HIGH, "profanity"},
        {"fucking", ToxicityLevel::HIGH, "profanity"},
        {"shit", ToxicityLevel::HIGH, "profanity"},
        {"shitty", ToxicityLevel::HIGH, "profanity"},
        {"bullshit", ToxicityLevel::HIGH, "profanity"},
        {"asshole", ToxicityLevel::HIGH, "insult"},
        {"bastard", ToxicityLevel::HIGH, "insult"},
        {"bitch", ToxicityLevel::HIGH, "insult"},
        {"idiot", ToxicityLevel::HIGH, "insult"},
        {"stupid", ToxicityLevel::HIGH, "insult"},
        {"dumb", ToxicityLevel::MEDIUM, "insult"},
        {"moron", ToxicityLevel::HIGH, "insult"},
        {"retard", ToxicityLevel::HIGH, "slur"},
        {"crazy", ToxicityLevel::LOW, "ableist"},
        {"insane", ToxicityLevel::LOW, "ableist"},

        // Hate speech and discrimination (based on research literature on harmful language)
        {"racist", ToxicityLevel::HIGH, "hate_speech"},
        {"sexist", ToxicityLevel::HIGH, "hate_speech"},
        {"homophobic", ToxicityLevel::HIGH, "hate_speech"},

        // Threat and harassment indicators
        {"kill", ToxicityLevel::HIGH, "threat"},
        {"die", ToxicityLevel::MEDIUM, "threat"},
        {"hurt", ToxicityLevel::MEDIUM, "threat"},
        {"rape", ToxicityLevel::HIGH, "threat"},
        {"violence", ToxicityLevel::HIGH, "threat"},

        // Self-harm indicators
        {"suicide", ToxicityLevel::HIGH, "self_harm"},
        {"cutting", ToxicityLevel::HIGH, "self_harm"},
        {"depressed", ToxicityLevel::LOW, "mental_health"},
        {"suicidal", ToxicityLevel::HIGH, "self_harm"},
    };
}

void ToxicityDetector::index_words() {
    for (size_t i = 0; i < toxic_words_.size(); ++i) {
        const std::string normalized = to_lower(toxic_words_[i].word);
        word_index_[normalized].push_back(i);
    }
}

std::string ToxicityDetector::normalize_text(const std::string& text) const {
    std::string normalized = to_lower(text);
    normalized = remove_non_alphanumeric(normalized);
    return normalized;
}

std::vector<ToxicityMatch> ToxicityDetector::find_toxic_words(
    const std::string& normalized,
    const std::string& original) {

    std::vector<ToxicityMatch> matches;

    (void)normalized;

    size_t token_start = std::string::npos;
    std::string word;

    auto flush_token = [&]() {
        if (word.empty()) {
            return;
        }

        auto it = word_index_.find(word);
        if (it != word_index_.end()) {
            for (size_t idx : it->second) {
                const auto& toxic_word = toxic_words_[idx];

                ToxicityMatch match;
                match.matched_text = word;
                match.start_pos = token_start;
                match.end_pos = token_start + word.length();
                match.level = toxic_word.level;
                match.category = toxic_word.category;
                matches.push_back(match);
            }
        }

        word.clear();
        token_start = std::string::npos;
    };

    for (size_t i = 0; i < original.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(original[i]);
        if (std::isalnum(c)) {
            if (token_start == std::string::npos) {
                token_start = i;
            }
            word += static_cast<char>(std::tolower(c));
        } else {
            flush_token();
        }
    }
    flush_token();

    const std::string original_lower = to_lower(original);
    for (const auto& toxic_word : toxic_words_) {
        if (toxic_word.word.find(' ') == std::string::npos) continue;
        const std::string phrase = to_lower(toxic_word.word);
        size_t position = 0;
        while ((position = original_lower.find(phrase, position)) != std::string::npos) {
            const size_t end = position + phrase.size();
            const bool left_boundary = position == 0 ||
                !std::isalnum(static_cast<unsigned char>(original_lower[position - 1]));
            const bool right_boundary = end == original_lower.size() ||
                !std::isalnum(static_cast<unsigned char>(original_lower[end]));
            if (left_boundary && right_boundary) {
                ToxicityMatch match;
                match.matched_text = phrase;
                match.start_pos = position;
                match.end_pos = end;
                match.level = toxic_word.level;
                match.category = toxic_word.category;
                matches.push_back(std::move(match));
            }
            position = end;
        }
    }

    std::sort(matches.begin(), matches.end(), [](const ToxicityMatch& a, const ToxicityMatch& b) {
        if (a.start_pos != b.start_pos) return a.start_pos < b.start_pos;
        const size_t a_length = a.end_pos - a.start_pos;
        const size_t b_length = b.end_pos - b.start_pos;
        if (a_length != b_length) return a_length > b_length;
        return a.level > b.level;
    });

    std::vector<ToxicityMatch> non_overlapping;
    for (const auto& candidate : matches) {
        const bool overlaps = std::any_of(
            non_overlapping.begin(), non_overlapping.end(),
            [&](const ToxicityMatch& accepted) {
                return candidate.start_pos < accepted.end_pos && candidate.end_pos > accepted.start_pos;
            });
        if (!overlaps) non_overlapping.push_back(candidate);
    }

    return non_overlapping;
}

ToxicityLevel ToxicityDetector::compute_overall_level(
    const std::vector<ToxicityMatch>& matches) {

    if (matches.empty()) {
        return ToxicityLevel::CLEAN;
    }

    ToxicityLevel max_level = ToxicityLevel::CLEAN;
    for (const auto& match : matches) {
        if (match.level > max_level) {
            max_level = match.level;
        }
    }

    // If high toxicity found, use that
    if (max_level == ToxicityLevel::HIGH) {
        return ToxicityLevel::HIGH;
    }

    // Count medium level matches
    int medium_count = 0;
    for (const auto& match : matches) {
        if (match.level == ToxicityLevel::MEDIUM) {
            medium_count++;
        }
    }

    // If multiple medium matches, escalate to high
    if (medium_count >= 2) {
        return ToxicityLevel::HIGH;
    }

    if (max_level == ToxicityLevel::MEDIUM) {
        return ToxicityLevel::MEDIUM;
    }

    return max_level;
}

ToxicityResult ToxicityDetector::analyze(const std::string& text) {
    ToxicityResult result;
    result.original_text = text;
    result.cleaned_text = text;

    if (text.empty()) {
        return result;
    }

    std::string normalized = normalize_text(text);
    auto matches = find_toxic_words(normalized, text);

    result.overall_level = compute_overall_level(matches);
    result.matches = matches;

    // Confidence increases with severity and corroborating matches.
    if (matches.empty()) {
        result.confidence = 0.98f;
    } else {
        int severity = 0;
        for (const auto& match : matches) severity = std::max(severity, static_cast<int>(match.level));
        result.confidence = std::min(0.99f, 0.55f + severity * 0.1f + matches.size() * 0.04f);
    }

    // Generate cleaned text by censoring toxic words
    result.cleaned_text = text;
    std::sort(matches.begin(), matches.end(),
        [](const ToxicityMatch& a, const ToxicityMatch& b) {
            return a.start_pos > b.start_pos;
        });

    for (const auto& match : matches) {
        if (match.level >= ToxicityLevel::MEDIUM) {
            std::string censor(match.end_pos - match.start_pos, '*');
            result.cleaned_text.replace(match.start_pos,
                                       match.end_pos - match.start_pos,
                                       censor);
        }
    }

    return result;
}

} // namespace nlp
