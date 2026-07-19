#pragma once

#include "toxicity_detector.h"
#include <string>
#include <vector>

namespace nlp {

class TextDetoxifier {
public:
    /// 初始化使用内置毒性检测器的去毒器。
    TextDetoxifier();
    /// 释放去毒器资源。
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

    /// 检测有毒文本范围，并返回静音元数据和报告。
    DetoxifiedText detoxify(const std::string& text);
    /// 将检测到的字符范围替换为用于显示的星号标记。
    std::string apply_censoring(const std::string& text, const ToxicityResult& analysis);

private:
    ToxicityDetector detector_;

    /// 格式化便于阅读的去毒报告。
    std::string build_report(const ToxicityResult& original, int censored);
};

} // namespace nlp
