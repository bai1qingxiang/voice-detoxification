#include "nlp/text_detoxifier.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

/// Terminates the test executable with a diagnostic when an assertion fails.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

/// Verifies toxic text is marked for muting while non-toxic text is preserved.
void expect_silence_redaction_marker(
    nlp::TextDetoxifier& detoxifier,
    const std::string& input,
    const std::string& preserved = {}) {

    const auto result = detoxifier.detoxify(input);
    expect(result.original_toxicity != nlp::ToxicityLevel::CLEAN,
           "toxic English phrase was not detected: " + input);
    expect(result.detoxified.find('*') != std::string::npos,
           "detected phrase was not marked for silence redaction: " + result.detoxified);
    expect(result.censored_words > 0 && !result.matches.empty(),
           "audio redaction timestamps have no toxicity matches");
    if (!preserved.empty()) {
        expect(result.detoxified.find(preserved) != std::string::npos,
               "non-toxic sentence content was lost: " + result.detoxified);
    }
    expect(result.final_toxicity == nlp::ToxicityLevel::CLEAN,
           "detoxified text is still classified as toxic: " + result.detoxified);
}

} // namespace

/// Runs English toxicity and silence-redaction regression cases.
int main() {
    nlp::TextDetoxifier detoxifier;

    expect_silence_redaction_marker(
        detoxifier,
        "Hello, fuck you, but please listen to the rest of this sentence.",
        "please listen to the rest of this sentence");

    expect_silence_redaction_marker(
        detoxifier,
        "I will kill you tomorrow, then the meeting continues at nine.",
        "the meeting continues at nine");

    expect_silence_redaction_marker(
        detoxifier,
        "You are an idiot, and this final clause must remain audible.",
        "this final clause must remain audible");

    expect_silence_redaction_marker(
        detoxifier,
        "She said, \"shut up\", before finishing the quoted sentence.",
        "before finishing the quoted sentence");

    expect_silence_redaction_marker(
        detoxifier,
        "What the fuck happened here? We still need the complete explanation.",
        "We still need the complete explanation");

    const std::string clean = "This complete English sentence should remain unchanged.";
    const auto clean_result = detoxifier.detoxify(clean);
    expect(clean_result.detoxified == clean, "clean English text changed unexpectedly");
    expect(clean_result.final_toxicity == nlp::ToxicityLevel::CLEAN,
           "clean English text was misclassified");

    std::cout << "All English silence-redaction regression tests passed.\n";
    return 0;
}
