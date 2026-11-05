from pathlib import Path
import re
from docx import Document
from docx.shared import Cm, Pt, RGBColor
from docx.oxml.ns import qn

root = Path(__file__).resolve().parent.parent
folder = root / '报告文档'
doc = Document(folder / '模板_补充概要设计报告.docx')
parts = re.findall(r'^## \d+ ([^\n]+)\n\n```mermaid\n(.*?)\n```\n\n([^\n]+)', (folder / '概要设计图代码.md').read_text(encoding='utf-8'), re.M | re.S)
assert len(parts) == 3

# Move the appendix codes into their actual chapter locations.
appendix = next(p for p in doc.paragraphs if p.text == '附录 A 设计图代码')
body = doc._element.body
start = list(body).index(appendix._p)
for element in list(body)[start:]:
    if element.tag != qn('w:sectPr'):
        body.remove(element)

locations = [
    ('3.2 系统结构设计', '包图', '图 3-4 系统逻辑包依赖图', '绘图位置：第 3.1 节系统组织设计末尾。各包为源文件的逻辑分组，箭头表示依赖。'),
    ('3.3 系统接口设计', '类图', '图 3-5 核心设计类图', '绘图位置：第 3.2 节设计类结构末尾。应保留继承、组合以及非拥有引用的区别。'),
    ('4 系统出错处理设计', '数据流图', '图 3-6 电路编辑与存储数据流图', '绘图位置：第 3.4 节末尾。此图为面向过程的补充视角，不表示函数调用顺序。'),
]
for heading, title, caption, note in locations:
    anchor = next(p for p in doc.paragraphs if p.text == heading)
    _, code, explanation = next(x for x in parts if x[0] == title)
    p = anchor.insert_paragraph_before(f'【此处插入{title}】{caption}')
    p.paragraph_format.keep_with_next = True
    for r in p.runs:
        r.bold = True
        r.font.color.rgb = RGBColor(180, 35, 35)
    anchor.insert_paragraph_before(note)
    anchor.insert_paragraph_before(explanation.replace('`', ''))
    p = anchor.insert_paragraph_before('对应绘图代码（Mermaid）：')
    p.paragraph_format.keep_with_next = True
    for line in code.splitlines():
        p = anchor.insert_paragraph_before(line)
        p.paragraph_format.left_indent = Cm(.4)
        p.paragraph_format.space_after = Pt(0)
        p.paragraph_format.line_spacing = 1.0
        for r in p.runs:
            r.font.name = 'Consolas'
            r.font.size = Pt(9)
            r._element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'), '宋体')

out = folder / '模板_标注绘图位置及代码.docx'
doc.save(out)
check = Document(out)
assert sum('【此处插入' in p.text for p in check.paragraphs) == 3
print(out)
