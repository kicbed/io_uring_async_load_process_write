# How to Use These Files

解压后，把这些文件放到 AsyncDataLoader 项目根目录。

推荐最终结构：

AsyncDataLoader/
  AGENTS.md
  docs/
    CODEX_WORKFLOW.md
    CODEX_START_PROMPTS.txt
    project_manual.md
    staged_prompts.txt
    anti_collapse_checklist.md
    stage_summaries/
  .agents/
    skills/
      asyncdataloader-cpp-coach/
        SKILL.md

如果你想先创建目录，可以运行：

bash setup_codex_files.sh

然后把你已有的三份文档放到：

docs/project_manual.md
docs/staged_prompts.txt
docs/anti_collapse_checklist.md

启动 Codex：

codex

第一次进入后，复制 docs/CODEX_START_PROMPTS.txt 里的“第一次启动 Codex 时使用”那段。
