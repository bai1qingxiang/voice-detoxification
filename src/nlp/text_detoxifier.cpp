#include "nlp/text_detoxifier.h"

#include <sstream>
#include <iomanip>
#include <map>
#include <algorithm>

namespace nlp {

namespace {

std::string toxicity_to_string(ToxicityLevel level) {
    switch (level) {
        case ToxicityLevel::CLEAN: return "CLEAN";
        case ToxicityLevel::LOW: return "LOW";
        case ToxicityLevel::MEDIUM: return "MEDIUM";
        case ToxicityLevel::HIGH: return "HIGH";
        default: return "UNKNOWN";
    }
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.length(), to);
        pos += to.length();
    }
}

void cleanup_replacement_artifacts(std::string& text) {
    replace_all(text, "broken up", "broken");
    replace_all(text, "frustrating it", "this is frustrating");
    replace_all(text, "you person", "you");
}

} // namespace

TextDetoxifier::TextDetoxifier(const DetoxificationOptions& options)
    : options_(options) {}

TextDetoxifier::~TextDetoxifier() = default;

std::vector<std::string> TextDetoxifier::get_alternatives(const std::string& toxic_word) {
    // Map of toxic words to suggested alternatives
    static const std::map<std::string, std::vector<std::string>> alternatives = {
        {"damn", {"frustrating"}},
        {"hell", {"heck"}},
        {"bullshit", {"nonsense", "rubbish"}},
        {"fuck", {""}},
        {"fucked", {"broken"}},
        {"fucking", {"very"}},
        {"shit", {"issue", "mess"}},
        {"shitty", {"poor"}},
        {"asshole", {"inconsiderate person"}},
        {"bastard", {"person"}},
        {"idiot", {"person"}},
        {"stupid", {"unhelpful"}},
        {"dumb", {"unclear"}},
        {"moron", {"person"}},
        {"retard", {""},},  // No good alternative; should censor
        {"crazy", {"unusual", "wild"}},
        {"insane", {"extreme", "wild"}},
        {"kill", {"defeat", "end"}},
        {"die", {"stop", "end"}},
        {"hurt", {"harm", "wound"}},
    };

    auto it = alternatives.find(toxic_word);
    if (it != alternatives.end()) {
        return it->second;
    }
    return {""};  // Return empty alternative if not found
}

std::string TextDetoxifier::choose_best_alternative(
    const std::string& toxic_word,
    const std::string& context) {

    auto alternatives = get_alternatives(toxic_word);
    if (alternatives.empty() || alternatives[0].empty()) {
        return "";  // No alternative available
    }

    // For now, just return the first alternative
    // In a more advanced version, this could use ML to pick the best one based on context
    return alternatives[0];
}

std::string TextDetoxifier::apply_censoring(
    const std::string& text,
    const ToxicityResult& analysis) {

    std::string result = text;

    // Sort matches by position in reverse order so replacements don't affect positions
    auto sorted_matches = analysis.matches;
    std::sort(sorted_matches.begin(), sorted_matches.end(),
        [](const ToxicityMatch& a, const ToxicityMatch& b) {
            return a.start_pos > b.start_pos;
        });

    for (const auto& match : sorted_matches) {
        std::string censor(match.end_pos - match.start_pos, '*');
        result.replace(match.start_pos,
                      match.end_pos - match.start_pos,
                      censor);
    }

    return result;
}

TextDetoxifier::DetoxifiedText TextDetoxifier::detoxify(const std::string& text) {
    DetoxifiedText result;
    result.original = text;
    result.detoxified = text;

    // Analyze toxicity
    auto analysis = detector_.analyze(text);
    result.original_toxicity = analysis.overall_level;

    if (analysis.matches.empty()) {
        result.final_toxicity = ToxicityLevel::CLEAN;
        result.report = "No toxic content detected.";
        return result;
    }

    // Apply detoxification
    if (options_.censor_only) {
        result.detoxified = apply_censoring(text, analysis);
        result.censored_words = analysis.matches.size();
    } else {
        // Try to replace with alternatives first
        result.detoxified = text;

        auto sorted_matches = analysis.matches;
        std::sort(sorted_matches.begin(), sorted_matches.end(),
            [](const ToxicityMatch& a, const ToxicityMatch& b) {
                return a.start_pos > b.start_pos;
            });

        for (const auto& match : sorted_matches) {
            // Check if we should process this category
            bool should_process = false;

            if (match.category == "profanity" && options_.remove_profanity) {
                should_process = true;
            } else if (match.category == "insult" && options_.remove_insults) {
                should_process = true;
            } else if (match.category == "slur" && options_.remove_slurs) {
                should_process = true;
            } else if (match.category == "ableist" && options_.remove_ableist) {
                should_process = true;
            } else if (match.category == "hate_speech" && options_.remove_hate_speech) {
                should_process = true;
            } else if (match.category == "threat" && options_.remove_threats) {
                should_process = true;
            } else if (match.category == "self_harm" && options_.remove_self_harm) {
                should_process = true;
            }

            if (!should_process) continue;

            // Try to find an alternative
            std::string alt = choose_best_alternative(match.matched_text, text);

            if (!alt.empty()) {
                result.detoxified.replace(match.start_pos,
                                         match.end_pos - match.start_pos,
                                         alt);
                result.replacements_made++;
            } else {
                // Censor if no alternative
                std::string censor(match.end_pos - match.start_pos, '*');
                result.detoxified.replace(match.start_pos,
                                         match.end_pos - match.start_pos,
                                         censor);
                result.censored_words++;
            }
        }

        cleanup_replacement_artifacts(result.detoxified);
    }

    // Re-analyze to get final toxicity level
    auto final_analysis = detector_.analyze(result.detoxified);
    result.final_toxicity = final_analysis.overall_level;

    // Build report
    result.report = build_report(analysis, result.replacements_made, result.censored_words);

    return result;
}

std::string TextDetoxifier::build_report(
    const ToxicityResult& original,
    int replacements,
    int censored) {

    std::stringstream ss;
    ss << "Detoxification Report:\n";
    ss << "  Original toxicity: " << toxicity_to_string(original.overall_level) << "\n";
    ss << "  Toxic words found: " << original.matches.size() << "\n";
    ss << "  Words replaced: " << replacements << "\n";
    ss << "  Words censored: " << censored << "\n";

    if (!original.matches.empty()) {
        ss << "  Detected issues:\n";
        for (const auto& match : original.matches) {
            ss << "    - [" << match.category << "] \"" << match.matched_text << "\" "
               << "(" << toxicity_to_string(match.level) << ")\n";
        }
    }

    return ss.str();
}

} // namespace nlp
