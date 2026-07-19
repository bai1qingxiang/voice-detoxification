#pragma once

#include "toxicity_detector.h"
#include <string>
#include <vector>

namespace nlp {

class TextDetoxifier {
public:
    /// Initializes a detoxifier backed by the built-in toxicity detector.
    TextDetoxifier();
    /// Releases detoxifier resources.
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

    /// Detects toxic text spans and returns redaction metadata and a report.
    DetoxifiedText detoxify(const std::string& text);
    /// Replaces detected character spans with asterisk markers for display.
    std::string apply_censoring(const std::string& text, const ToxicityResult& analysis);

private:
    ToxicityDetector detector_;

    /// Formats a human-readable detoxification report.
    std::string build_report(const ToxicityResult& original, int censored);
};

} // namespace nlp
