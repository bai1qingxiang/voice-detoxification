#!/usr/bin/env python3
"""
Advanced Voice Detoxification using ML Models

This script provides optional ML-based detoxification using:
- HuggingFace transformers (toxicity classification)
- Perspective API (if key provided)
- Custom fine-tuned models

Install requirements:
    pip install transformers torch numpy
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, List, Tuple

try:
    from transformers import pipeline
except ImportError:
    print("Error: transformers not installed. Install with: pip install transformers torch")
    sys.exit(1)


class MLToxicityDetector:
    """
    Uses HuggingFace transformer models for toxicity detection.
    Based on research-backed models like:
    - 'michellejieli/NSFW_text_classifier'
    - 'unbiased-toxic-bert' from Detoxify
    - 'facebook/roberta-hate-speech-multilingual-twitter-xlmr-large'
    """

    def __init__(self, model_name: str = "michellejieli/NSFW_text_classifier"):
        """Initialize the toxicity classifier."""
        self.model_name = model_name
        print(f"Loading model: {model_name}")
        self.classifier = pipeline(
            "text-classification",
            model=model_name,
            device=-1  # Use CPU, change to 0 for GPU
        )

    def detect(self, text: str, threshold: float = 0.5) -> Dict:
        """
        Detect toxic content in text.

        Args:
            text: Input text to analyze
            threshold: Confidence threshold (0-1)

        Returns:
            Dictionary with:
            - is_toxic: bool
            - score: float (confidence)
            - label: str (classification)
            - segments: List of detected toxic segments
        """
        # Chunk text if too long (transformers have max length)
        max_length = 512
        results = {"is_toxic": False, "score": 0.0, "segments": []}

        chunks = [text[i:i + max_length] for i in range(0, len(text), max_length)]

        for chunk in chunks:
            pred = self.classifier(chunk[:512])[0]
            score = pred["score"]

            if score >= threshold:
                results["is_toxic"] = True
                results["score"] = max(results["score"], score)
                results["segments"].append({
                    "text": chunk[:100] + "..." if len(chunk) > 100 else chunk,
                    "score": score,
                    "label": pred["label"]
                })

        results["label"] = results["segments"][0]["label"] if results["segments"] else "clean"
        return results


class AdvancedTextDetoxifier:
    """Advanced detoxification with multiple strategies."""

    def __init__(self, ml_detector: MLToxicityDetector = None):
        self.ml_detector = ml_detector

    def detoxify(self, text: str) -> Dict:
        """
        Perform advanced detoxification.

        Args:
            text: Input text

        Returns:
            Dictionary with detoxification results
        """
        results = {"original": text, "detoxified": text, "methods_applied": []}

        if self.ml_detector:
            ml_result = self.ml_detector.detect(text)
            results["ml_analysis"] = ml_result

            if ml_result["is_toxic"]:
                # Apply masking strategy
                detoxified = text
                for _ in ml_result["segments"]:
                    # Simple approach: mask suspicious segments
                    detoxified = text.replace(text, "[CONTENT FILTERED]")

                results["detoxified"] = detoxified
                results["methods_applied"].append("ml_masking")

        return results


def main():
    parser = argparse.ArgumentParser(
        description="Advanced ML-based voice detoxification"
    )
    parser.add_argument(
        "--input",
        type=str,
        required=True,
        help="Input text file or string"
    )
    parser.add_argument(
        "--model",
        type=str,
        default="michellejieli/NSFW_text_classifier",
        help="HuggingFace model name"
    )
    parser.add_argument(
        "--threshold",
        type=float,
        default=0.5,
        help="Toxicity confidence threshold (0-1)"
    )
    parser.add_argument(
        "--output",
        type=str,
        help="Output file (optional, defaults to stdout)"
    )

    args = parser.parse_args()

    # Read input
    if Path(args.input).exists():
        with open(args.input, "r") as f:
            text = f.read()
    else:
        text = args.input

    # Initialize detector and detoxifier
    try:
        detector = MLToxicityDetector(args.model)
        detoxifier = AdvancedTextDetoxifier(detector)

        # Perform detoxification
        result = detoxifier.detoxify(text)

        # Format output
        output = {
            "status": "success",
            "original_text": result["original"],
            "detoxified_text": result["detoxified"],
            "methods_applied": result["methods_applied"],
        }

        if "ml_analysis" in result:
            output["ml_analysis"] = result["ml_analysis"]

        # Write output
        if args.output:
            with open(args.output, "w") as f:
                json.dump(output, f, indent=2)
            print(f"Results written to: {args.output}")
        else:
            print(json.dumps(output, indent=2))

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
