#include "nlp/text_detoxifier.h"

#include <sstream>
#include <iomanip>
#include <algorithm>

namespace nlp {

namespace {

/// 将毒性枚举值转换为便于阅读的报告标签。
std::string toxicity_to_string(ToxicityLevel level) {
    switch (level) {
        case ToxicityLevel::CLEAN: return "CLEAN";
        case ToxicityLevel::LOW: return "LOW";
        case ToxicityLevel::MEDIUM: return "MEDIUM";
        case ToxicityLevel::HIGH: return "HIGH";
        default: return "UNKNOWN";
    }
}

} // namespace

/// 初始化文本去毒器及其毒性检测器。
TextDetoxifier::TextDetoxifier() = default;

/// 释放文本去毒器持有的资源。
TextDetoxifier::~TextDetoxifier() = default;

/// 将检测到的有毒字符范围替换为等长星号。
std::string TextDetoxifier::apply_censoring(
    const std::string& text,
    const ToxicityResult& analysis) {

    std::string result = text;

    // Sort by position in reverse order so earlier redaction offsets stay valid.
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

/// 检测有毒范围，并生成文本标记和音频静音元数据。
TextDetoxifier::DetoxifiedText TextDetoxifier::detoxify(const std::string& text) {
    DetoxifiedText result;
    result.original = text;
    result.detoxified = text;

    // Analyze toxicity
    auto analysis = detector_.analyze(text);
    result.original_toxicity = analysis.overall_level;
    result.matches = analysis.matches;

    if (analysis.matches.empty()) {
        result.final_toxicity = ToxicityLevel::CLEAN;
        result.report = "No toxic content detected.";
        return result;
    }

    // Text is only a report of the audio redaction. The output audio keeps the
    // original recording and silences these exact time ranges.
    result.detoxified = apply_censoring(text, analysis);
    result.censored_words = static_cast<int>(analysis.matches.size());

    // Re-analyze to get final toxicity level
    auto final_analysis = detector_.analyze(result.detoxified);
    result.final_toxicity = final_analysis.overall_level;

    // Build report
    result.report = build_report(analysis, result.censored_words);

    return result;
}

/// 根据检测范围和严重程度生成详细文本报告。
std::string TextDetoxifier::build_report(
    const ToxicityResult& original,
    int censored) {

    std::stringstream ss;
    ss << "Detoxification Report:\n";
    ss << "  Original toxicity: " << toxicity_to_string(original.overall_level) << "\n";
    ss << "  Toxic words found: " << original.matches.size() << "\n";
    ss << "  Audio ranges to mute: " << censored << "\n";

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
