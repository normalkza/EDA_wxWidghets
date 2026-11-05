from pathlib import Path
from docx import Document
from docx.shared import Cm, Pt, RGBColor
from docx.oxml.ns import qn

root = Path(__file__).resolve().parent.parent
folder = root / '报告文档'
doc = Document(folder / '模板_标注绘图位置及代码.docx')
diagrams = [
('系统内部模块关系', '''flowchart TB
  UI[用户操作：菜单、工具、属性]
  Editor[MainFrame 与 Canvas：命令协调和画布编辑]
  Logic[Component 与 Circuit：元件行为和逻辑传播]
  Geometry[Wire：路径与命中判定]
  Storage[CircuitFile：XML 校验与读写]
  UI --> Editor
  Editor --> Logic
  Editor --> Geometry
  Editor --> Storage'''),
('主要系统用例', '''flowchart LR
  User[本地使用者]
  subgraph System[数字电路编辑与逻辑仿真系统]
    Edit([元件编辑与参数设置])
    Connect([引脚连接与线路调整])
    Evaluate([电平切换与逻辑计算])
    Text([文字编辑、剪贴板与撤销])
    Document([新建、打开与保存文档])
  end
  User --- Edit
  User --- Connect
  User --- Evaluate
  User --- Text
  User --- Document'''),
('逻辑传播的主要处理流程', '''flowchart TB
  Start([开始]) --> Build[整理有效元件，清理无效连接，建立入度和出边]
  Build --> Init[输入引脚初始化为低电平，零入度元件入队]
  Init --> Ready{队列是否非空？}
  Ready -- 是 --> Eval[出队一个元件，调用 Evaluate，已处理数加一]
  Eval --> Propagate[沿出边传播输出，减少后继入度]
  Propagate --> Enqueue[将入度变为零的后继加入队列]
  Enqueue --> Ready
  Ready -- 否 --> Complete{已处理数是否等于元件总数？}
  Complete -- 是 --> Success([返回 true])
  Complete -- 否 --> Failure([返回 false])'''),
]
images = [p for p in doc.paragraphs if p._p.xpath('.//w:drawing')]
assert len(images) == len(diagrams)
for image, (title, code) in zip(images, diagrams):
    marker = image.insert_paragraph_before('【此处插入图】' + title)
    marker.paragraph_format.keep_with_next = True
    for run in marker.runs:
        run.bold = True
        run.font.color.rgb = RGBColor(180, 35, 35)
    image.insert_paragraph_before('对应绘图代码（Mermaid）：').paragraph_format.keep_with_next = True
    for line in code.splitlines():
        p = image.insert_paragraph_before(line)
        p.paragraph_format.left_indent = Cm(.4)
        p.paragraph_format.space_after = Pt(0)
        p.paragraph_format.line_spacing = 1.0
        for run in p.runs:
            run.font.name = 'Consolas'
            run.font.size = Pt(9)
            run._element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'), '宋体')
    image._p.getparent().remove(image._p)

# Remove obsolete image relationships as well as the visible drawings.
for rel_id, rel in list(doc.part.rels.items()):
    if rel.reltype.endswith('/image'):
        doc.part.drop_rel(rel_id)
out = folder / '模板_全部图替换为代码.docx'
doc.save(out)
check = Document(out)
assert len(check.inline_shapes) == 0
assert sum('对应绘图代码（Mermaid）' in p.text for p in check.paragraphs) == 6
print(out)
