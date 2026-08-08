from pathlib import Path
from tempfile import NamedTemporaryFile
from zipfile import ZIP_DEFLATED, ZipFile
from xml.sax.saxutils import escape
import re


ROOT = Path(r"D:\develop\programfirst")
PPTX = ROOT / "参赛材料" / "智境-物联网设计大赛答辩PPT.pptx"


REPLACEMENTS = {
    "结论：智境已具备可录制、可解释、可扩展的比赛展示能力": "总结：智境完成了从真实感知到智能调控的闭环验证",
    "答辩结束语": "答辩总结",
    "项目已经完成真实传感器采集、本地可视化、现场执行控制和 4G 在线能力验证。后续通过平台物模型完善和 PCB 载板集成，可以继续向可部署的室内环境智能节点演进。": (
        "本项目以 STM32F103C8T6 与 FreeRTOS 为核心，完成了环境数据采集、实时任务调度、OLED 显示、舵机/声光执行、串口可视化和 4G DTU 平台管理的完整链路。"
        "系统数据来自板端真实传感器，现场可通过光照、温度和按键变化直接触发状态更新，具备清晰的演示效果和工程扩展价值。"
        "后续可继续扩展湿度、CO2、人体存在等传感器，并通过一体化 PCB 载板提升样机集成度。"
    ),
    "谢谢观看 / Q&amp;A": "感谢各位评委老师，欢迎提问",
}


def replace_text_nodes(xml):
    def repl(match):
        content = match.group(1)
        if content in REPLACEMENTS:
            return f"<a:t>{escape(REPLACEMENTS[content])}</a:t>"
        return match.group(0)

    return re.sub(r"<a:t>(.*?)</a:t>", repl, xml)


def main():
    if not PPTX.exists():
        raise FileNotFoundError(PPTX)

    with ZipFile(PPTX, "r") as zin:
        slide_names = sorted(
            [
                name
                for name in zin.namelist()
                if name.startswith("ppt/slides/slide") and name.endswith(".xml")
            ],
            key=lambda s: int(re.search(r"slide(\d+)\.xml", s).group(1)),
        )
        last_slide = slide_names[-1]

        with NamedTemporaryFile(delete=False, suffix=".pptx", dir=PPTX.parent) as tmp:
            tmp_path = Path(tmp.name)

        with ZipFile(tmp_path, "w", ZIP_DEFLATED) as zout:
            for item in zin.infolist():
                data = zin.read(item.filename)
                if item.filename == last_slide:
                    xml = data.decode("utf-8", errors="ignore")
                    xml = replace_text_nodes(xml)
                    data = xml.encode("utf-8")
                zout.writestr(item, data)

    tmp_path.replace(PPTX)
    print(PPTX)


if __name__ == "__main__":
    main()
