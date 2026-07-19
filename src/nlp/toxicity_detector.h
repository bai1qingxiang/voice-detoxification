#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace nlp {

enum class ToxicityLevel {
    CLEAN = 0,
    LOW = 1,
    MEDIUM = 2,
    HIGH = 3,
};

struct ToxicityMatch {
    std::string matched_text;
    size_t start_pos = 0;
    size_t end_pos = 0;
    ToxicityLevel level = ToxicityLevel::LOW;
    std::string category;
};

struct ToxicityResult {
    ToxicityLevel overall_level = ToxicityLevel::CLEAN;
    float confidence = 0.0f;
    std::vector<ToxicityMatch> matches;
    std::string original_text;
    std::string cleaned_text;
};

class ToxicityDetector {
public:
    /// Initializes the built-in English toxicity patterns.
    ToxicityDetector();
    /// Releases detector resources.
    ~ToxicityDetector();

    /// Detects toxic words and phrases in input text.
    ToxicityResult analyze(const std::string& text);

private:
    struct ToxicWord {
        std::string word;
        ToxicityLevel level;
        std::string category;
    };

    std::vector<ToxicWord> toxic_words_;
    std::unordered_map<std::string, std::vector<size_t>> word_index_;

    /// Populates the built-in pattern list.
    void initialize_toxic_wordlist();
    /// Builds the normalized word lookup table.
    void index_words();
    /// Normalizes input for matching.
    std::string normalize_text(const std::string& text) const;
    /// Finds non-overlapping matches and their original offsets.
    std::vector<ToxicityMatch> find_toxic_words(const std::string& normalized, const std::string& original);
    /// Computes the maximum severity of all matches.
    ToxicityLevel compute_overall_level(const std::vector<ToxicityMatch>& matches);
};

} // namespace nlp
