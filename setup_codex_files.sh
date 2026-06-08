#!/usr/bin/env bash
set -euo pipefail

mkdir -p docs/stage_summaries
mkdir -p include/backend include/coroutine include/buffer include/pipeline include/metrics include/config include/util
mkdir -p src examples benchmark tests
mkdir -p .agents/skills/asyncdataloader-cpp-coach

echo "Project folders prepared."
echo "Now place your documents as:"
echo "  docs/project_manual.md"
echo "  docs/staged_prompts.txt"
echo "  docs/anti_collapse_checklist.md"
