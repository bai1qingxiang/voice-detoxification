#pragma once

#include "toxicity_detector.h"
#include <string>
#include <vector>

namespace nlp {

struct DetoxificationOptions {
    bool censor_only = false;  // If true, just replace with *, if false, try to replace with alternatives
    bool remove_profanity = true;
    bool remove_hate_speech = true;
    bool remove_threats = true;
    bool remove_self_harm = true;
};

class TextDetoxifier {
public:
    explicit TextDetoxifier(const DetoxificationOptions& options = {});
    ~TextDetoxifier();

    struct DetoxifiedText {
        std::string original;
        std::string detoxified;
        int replacements_made = 0;
        int censored_words = 0;
        ToxicityLevel original_toxicity;
        ToxicityLevel final_toxicity;
        std::string report;
    };

    DetoxifiedText detoxify(const std::string& text);
    std::string apply_censoring(const std::string& text, const ToxicityResult& analysis);

private:
    ToxicityDetector detector_;
    DetoxificationOptions options_;

    std::vector<std::string> get_alternatives(const std::string& toxic_word);
    std::string choose_best_alternative(const std::string& toxic_word, const std::string& context);
    std::string build_report(const ToxicityResult& original, int replacements, int censored);
};

} // namespace nlp
