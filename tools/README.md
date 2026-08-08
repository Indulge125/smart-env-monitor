# 工具脚本

本目录是**一次性内容生成脚本**，用于自动装配物联网设计大赛的参赛材料（PPT 工程源、计划书、视频文案、架构图），由 AI 工作流生成。

⚠️ 这些脚本不是维护中的工具，**包含本机绝对路径**（如 `D:\qqdownload\...`、`D:\BaiduNetdiskDownload\...`）且依赖特定模板文档，仅在你自己的机器上可用。不要依赖它们重现材料；材料成品见 `参赛材料/`。

| 脚本 | 作用 |
| --- | --- |
| `build_zhijing_competition_materials.py` | 主脚本：生成计划书 DOCX、视频文案、架构图 PNG、PPT 源文件 |
| `extract_competition_context.py` | 从模板/项目文件抽取上下文（产物为 `参赛材料/*.txt`，不入库） |
| `update_ppt_last_slide.py` | 修正生成 PPT 的末页 |
| `update_zhijing_plan_template.py` | 基于本地模板按段落/表格索引回填计划书 |
