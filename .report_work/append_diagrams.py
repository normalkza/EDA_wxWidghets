from pathlib import Path
import re

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.shared import Cm, Pt, RGBColor
from docx.oxml.ns import qn

root = Path(__file__).resolve().parent.parent
source = root / '报告文档' / '第二模板_完整概要设计报告.docx'
code = root / '报告文档' / '概要设计图代码.md'
output = root / '报告文档' / '模板_补充概要设计报告.docx'

doc = Document(source)
doc.add_page_break()
doc.add_heading('附录 A 设计图代码', 1)
p = doc.add_paragraph('本附录提供包图、数据流图和类图的 Mermaid 源码。包图和类图对应面向对象设计；数据流图采用面向过程的视角，展示外部实体、处理过程和数据存储之间的数据流。')
p.paragraph_format.first_line_indent = Cm(0.74)

parts = re.findall(r'^## \d+ ([^\n]+)\n\n```mermaid\n(.*?)\n```\n\n([^\n]+)', code.read_text(encoding='utf-8'), re.M | re.S)
assert len(parts) == 3, len(parts)
for index, (title, diagram, explanation) in enumerate(parts, 1):
    doc.add_heading(f'A.{index} {title}', 2)
    p = doc.add_paragraph(explanation)
    p.paragraph_format.first_line_indent = Cm(0.74)
    intro = doc.add_paragraph('以下为可编辑的 Mermaid 源码：')
    intro.paragraph_format.keep_with_next = True
    for line in diagram.splitlines():
        p = doc.add_paragraph(style='Normal')
        p.paragraph_format.left_indent = Cm(0.6)
        p.paragraph_format.space_after = Pt(0)
        p.paragraph_format.line_spacing = 1.0
        run = p.add_run(line)
        run.font.name = 'Consolas'
        run.font.size = Pt(8)
        run.font.color.rgb = RGBColor(0, 0, 0)
        run._element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'), '宋体')
    if index < len(parts):
        doc.add_page_break()

doc.core_properties.title = '数字电路编辑与逻辑仿真系统概要设计报告'
doc.save(output)
check = Document(output)
full = '\n'.join(p.text for p in check.paragraphs)
for required in ['8 进度计划', '附录 A 设计图代码', 'A.1 包图', 'A.2 数据流图', 'A.3 类图', 'classDiagram', 'flowchart LR']:
    assert required in full, required
print(output, output.stat().st_size, len(check.paragraphs), len(check.tables))
