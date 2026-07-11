#!/usr/bin/env python3
"""
Evaluate detoxification quality with an independent toxicity model.

The default evaluator is Detoxify('unbiased'), a RoBERTa-based classifier
trained for toxicity detection while reducing unintended identity bias.
"""

import argparse
import csv
import json
import sys
from pathlib import Path
from statistics import mean
from typing import Any, Dict, Iterable, List, Optional


DEFAULT_MODEL = "unbiased"
DEFAULT_THRESHOLD = 0.5
DEFAULT_MAX_WORDS = 220


def read_text_value(value: str) -> str:
    path = Path(value)
    if path.exists():
        return path.read_text(encoding="utf-8")
    return value


def split_into_chunks(text: str, max_words: int = DEFAULT_MAX_WORDS) -> List[str]:
    words = text.split()
    if not words:
        return [""]

    chunks = []
    for start in range(0, len(words), max_words):
        chunks.append(" ".join(words[start : start + max_words]))
    return chunks


def load_pairs(
    path: Path,
    original_column: str,
    detoxified_column: str,
) -> List[Dict[str, str]]:
    suffix = path.suffix.lower()

    if suffix == ".csv":
        with path.open("r", encoding="utf-8", newline="") as f:
            reader = csv.DictReader(f)
            return [
                {
                    "id": row.get("id") or str(index + 1),
                    "original": row[original_column],
                    "detoxified": row[detoxified_column],
                }
                for index, row in enumerate(reader)
            ]

    if suffix == ".jsonl":
        pairs = []
        with path.open("r", encoding="utf-8") as f:
            for index, line in enumerate(f):
                if not line.strip():
                    continue
                row = json.loads(line)
                pairs.append(
                    {
                        "id": str(row.get("id") or index + 1),
                        "original": row[original_column],
                        "detoxified": row[detoxified_column],
                    }
                )
        return pairs

    if suffix == ".json":
        payload = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(payload, dict):
            payload = payload.get("items", [payload])
        return [
            {
                "id": str(row.get("id") or index + 1),
                "original": row[original_column],
                "detoxified": row[detoxified_column],
            }
            for index, row in enumerate(payload)
        ]

    raise ValueError("Pairs file must be .csv, .jsonl, or .json")


def scalarize(value: Any, index: int = 0) -> float:
    if hasattr(value, "tolist"):
        value = value.tolist()
    if isinstance(value, list):
        return float(value[index])
    return float(value)


class ToxicityJudge:
    def __init__(self, model_name: str, device: str):
        try:
            from detoxify import Detoxify
        except ImportError:
            print(
                "Error: detoxify is not installed. Install Python dependencies with: "
                "pip install -r scripts/requirements.txt",
                file=sys.stderr,
            )
            sys.exit(1)

        self.model = Detoxify(model_name, device=device)

    def score(self, text: str) -> Dict[str, float]:
        chunks = split_into_chunks(text)
        raw_scores = self.model.predict(chunks)

        label_scores: Dict[str, float] = {}
        for label, values in raw_scores.items():
            label_scores[label] = max(
                scalarize(values, index) for index in range(len(chunks))
            )

        if "toxicity" in label_scores:
            primary = label_scores["toxicity"]
        elif "toxic" in label_scores:
            primary = label_scores["toxic"]
        else:
            primary = max(label_scores.values(), default=0.0)
        label_scores["primary_toxicity"] = primary
        return label_scores


def evaluate_pair(
    judge: ToxicityJudge,
    item: Dict[str, str],
    threshold: float,
) -> Dict[str, Any]:
    original_scores = judge.score(item["original"])
    detoxified_scores = judge.score(item["detoxified"])

    original_score = original_scores["primary_toxicity"]
    detoxified_score = detoxified_scores["primary_toxicity"]
    reduction = original_score - detoxified_score
    relative_reduction = reduction / original_score if original_score > 0 else 0.0

    originally_toxic = original_score >= threshold
    residual_toxic = detoxified_score >= threshold

    return {
        "id": item["id"],
        "original_score": round(original_score, 6),
        "detoxified_score": round(detoxified_score, 6),
        "toxicity_reduction": round(reduction, 6),
        "relative_reduction": round(relative_reduction, 6),
        "originally_toxic": originally_toxic,
        "residual_toxic": residual_toxic,
        "detox_success": originally_toxic and not residual_toxic,
        "regressed": detoxified_score > original_score,
        "original_labels": original_scores,
        "detoxified_labels": detoxified_scores,
    }


def summarize(results: Iterable[Dict[str, Any]]) -> Dict[str, Any]:
    rows = list(results)
    toxic_rows = [row for row in rows if row["originally_toxic"]]

    success_rate: Optional[float] = None
    residual_rate: Optional[float] = None
    if toxic_rows:
        success_rate = mean(1.0 if row["detox_success"] else 0.0 for row in toxic_rows)
        residual_rate = mean(1.0 if row["residual_toxic"] else 0.0 for row in toxic_rows)

    return {
        "total_pairs": len(rows),
        "originally_toxic_pairs": len(toxic_rows),
        "detox_success_rate": success_rate,
        "residual_toxic_rate": residual_rate,
        "average_original_score": (
            mean(row["original_score"] for row in rows) if rows else 0.0
        ),
        "average_detoxified_score": (
            mean(row["detoxified_score"] for row in rows) if rows else 0.0
        ),
        "average_toxicity_reduction": (
            mean(row["toxicity_reduction"] for row in rows) if rows else 0.0
        ),
        "average_relative_reduction": (
            mean(row["relative_reduction"] for row in rows) if rows else 0.0
        ),
        "regression_rate": (
            mean(1.0 if row["regressed"] else 0.0 for row in rows) if rows else 0.0
        ),
    }


def print_report(summary: Dict[str, Any], results: List[Dict[str, Any]]) -> None:
    def pct(value: Optional[float]) -> str:
        if value is None:
            return "n/a"
        return f"{value * 100:.2f}%"

    print("Detoxification Accuracy Evaluation")
    print("=" * 38)
    print(f"Total pairs:                 {summary['total_pairs']}")
    print(f"Originally toxic pairs:      {summary['originally_toxic_pairs']}")
    print(f"Detox success rate:          {pct(summary['detox_success_rate'])}")
    print(f"Residual toxic rate:         {pct(summary['residual_toxic_rate'])}")
    print(f"Average original score:      {summary['average_original_score']:.4f}")
    print(f"Average detoxified score:    {summary['average_detoxified_score']:.4f}")
    print(f"Average toxicity reduction:  {summary['average_toxicity_reduction']:.4f}")
    print(f"Average relative reduction:  {pct(summary['average_relative_reduction'])}")
    print(f"Regression rate:             {pct(summary['regression_rate'])}")

    print("\nPer-pair scores")
    print("id,original,detoxified,reduction,success,residual")
    for row in results:
        print(
            f"{row['id']},{row['original_score']:.4f},"
            f"{row['detoxified_score']:.4f},{row['toxicity_reduction']:.4f},"
            f"{row['detox_success']},{row['residual_toxic']}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Evaluate detoxification accuracy with Detoxify."
    )
    input_group = parser.add_mutually_exclusive_group(required=True)
    input_group.add_argument(
        "--pairs",
        type=Path,
        help="CSV, JSONL, or JSON file containing original and detoxified columns.",
    )
    input_group.add_argument(
        "--original",
        help="Original text or path to a text file. Requires --detoxified.",
    )
    parser.add_argument(
        "--detoxified",
        help="Detoxified text or path to a text file when using --original.",
    )
    parser.add_argument("--original-column", default="original")
    parser.add_argument("--detoxified-column", default="detoxified")
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--device", default="cpu", help="Use cpu or cuda.")
    parser.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD)
    parser.add_argument("--output", type=Path, help="Write full JSON results.")

    args = parser.parse_args()

    if args.original and not args.detoxified:
        parser.error("--detoxified is required when using --original")

    if args.pairs:
        pairs = load_pairs(args.pairs, args.original_column, args.detoxified_column)
    else:
        pairs = [
            {
                "id": "1",
                "original": read_text_value(args.original),
                "detoxified": read_text_value(args.detoxified),
            }
        ]

    judge = ToxicityJudge(args.model, args.device)
    results = [evaluate_pair(judge, pair, args.threshold) for pair in pairs]
    summary = summarize(results)

    print_report(summary, results)

    if args.output:
        payload = {
            "model": args.model,
            "threshold": args.threshold,
            "summary": summary,
            "results": results,
        }
        args.output.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"\nFull JSON written to: {args.output}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
