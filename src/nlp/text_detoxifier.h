#pragma once

#include "toxicity_detector.h"
#include <string>
#include <vector>

namespace nlp {

class TextDetoxifier {
public:
    TextDetoxifier();
    ~TextDetoxifier();

    struct DetoxifiedText {
        std::string original;
        std::string detoxified;
        int censored_words = 0;
        std::vector<ToxicityMatch> matches;
        ToxicityLevel original_toxicity;
        ToxicityLevel final_toxicity;
        std::string report;
    };

    DetoxifiedText detoxify(const std::string& text);
    std::string apply_censoring(const std::string& text, const ToxicityResult& analysis);

private:
    ToxicityDetector detector_;

    std::string build_report(const ToxicityResult& original, int censored);
};

} // namespace nlp
