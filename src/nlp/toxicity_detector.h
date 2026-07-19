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
    /// 初始化内置英文毒性模式。
    ToxicityDetector();
    /// 释放检测器资源。
    ~ToxicityDetector();

    /// 检测输入文本中的有毒单词和短语。
    ToxicityResult analyze(const std::string& text);

private:
    struct ToxicWord {
        std::string word;
        ToxicityLevel level;
        std::string category;
    };

    std::vector<ToxicWord> toxic_words_;
    std::unordered_map<std::string, std::vector<size_t>> word_index_;

    /// 填充内置模式列表。
    void initialize_toxic_wordlist();
    /// 构建规范化单词查找表。
    void index_words();
    /// 规范化用于匹配的输入文本。
    std::string normalize_text(const std::string& text) const;
    /// 查找不重叠的匹配项及其原始偏移。
    std::vector<ToxicityMatch> find_toxic_words(const std::string& normalized, const std::string& original);
    /// 计算所有匹配项中的最高严重程度。
    ToxicityLevel compute_overall_level(const std::vector<ToxicityMatch>& matches);
};

} // namespace nlp
