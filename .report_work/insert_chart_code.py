from pathlib import Path
from docx import Document
from docx.shared import Pt
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.text.paragraph import Paragraph

root = Path(__file__).resolve().parent
d = Document(root / 'chronology_checked.docx')
codes = {}
codes['数据流图'] = '''@startuml
title 电路编辑与逻辑仿真系统一级数据流图
left to right direction
skinparam monochrome true
rectangle "使用者" as User
circle "1 电路编辑" as Edit
circle "2 逻辑求值" as Eval
circle "3 文件管理" as File
circle "4 结果展示" as View
collections "D1 电路工作数据" as Work
database "D2 XML电路文件" as XML
User --> Edit : 元件、连线及文字编辑信息
User --> Edit : 输入电平设置
Edit --> Work : 更新后的结构、参数和布局
Work --> Edit : 当前电路数据
Edit --> Eval : 电路结构及输入电平
Eval --> Work : 引脚逻辑状态
Eval --> View : 求值结果或失败信息
Edit --> View : 编辑结果或校验信息
Work --> View : 元件、线路、文字及逻辑状态
View --> User : 电路画面、输出电平及操作反馈
User --> File : 打开或保存请求及文件路径
Work --> File : 待保存电路文档
File --> XML : XML文件数据
XML --> File : XML文件数据
File --> Edit : 校验通过的待导入文档
File --> View : 文件操作结果
@enduml'''
codes['包图'] = '''@startuml
title 系统逻辑包及依赖关系
top to bottom direction
skinparam monochrome true
package "界面组织包" as UI {
  [MyApp / MainFrame]
  [MenuBar / ToolBar / Toolbox / PropertyPanel]
}
package "画布编辑包" as Editor {
  [Canvas]
  [Wire 几何与绘制]
}
package "电路模型包" as Model {
  [Component / Pin / LogicValue]
  [MultiInputGate / 各逻辑门 / 输入输出端口]
  [Circuit / PinReference / WireConnection]
}
package "文件管理包" as Storage {
  [CircuitFile]
}
package "文档数据包" as Data {
  [CircuitDocument]
  [元件、线路与文字记录]
}
UI ..> Editor : 编辑命令与状态访问
UI ..> Storage : 打开与保存
Editor ..> Model : 连接校验与逻辑求值
Editor ..> Data : 导入、导出与撤销快照
Storage ..> Data : XML读写
Storage ..> Model : 电路合法性校验
@enduml'''
codes['用例结构图'] = '''@startuml
title 系统用例结构图
left to right direction
skinparam monochrome true
actor "使用者" as User
rectangle "数字电路编辑与逻辑仿真系统" {
  usecase "新建电路" as New
  usecase "打开电路" as Open
  usecase "保存电路" as Save
  usecase "编辑元件" as Edit
  usecase "连接引脚" as Connect
  usecase "调整线路路径" as Route
  usecase "编辑文字" as Text
  usecase "撤销编辑" as Undo
  usecase "切换输入电平" as Toggle
  usecase "运行逻辑仿真" as Sim
  usecase "检查连接合法性" as Check
  usecase "显示逻辑状态" as Show
  usecase "校验电路文档" as Validate
}
User -- New
User -- Open
User -- Save
User -- Edit
User -- Connect
User -- Route
User -- Text
User -- Undo
User -- Toggle
User -- Sim
Connect ..> Check : <<include>>
Open ..> Validate : <<include>>
Save ..> Validate : <<include>>
Toggle ..> Sim : <<include>>
Sim ..> Show : <<include>>
@enduml'''
codes['类图'] = '''@startuml
title 核心设计类及关系
top to bottom direction
skinparam monochrome true
hide empty members
class MainFrame
class Canvas {
  +导入文档()
  +导出文档()
  +撤销编辑()
}
abstract class Component {
  名称
  +求值()
}
abstract class MultiInputGate
class AndGate
class OrGate
class NandGate
class NorGate
class XorGate
class NotGate
class InputComponent
class OutputComponent
class Pin {
  方向
  逻辑值
}
class Circuit {
  +连接引脚()
  +检查环路()
  +逻辑求值()
}
class WireConnection
class PinReference {
  方向
  引脚索引
}
class CircuitDocument
class CircuitFile {
  +读取()
  +校验()
  +保存()
}
MainFrame *-- "1" Canvas : 画布
Canvas *-- "0..*" Component : 管理元件
Canvas *-- "1" Circuit : 电路连接模型
Component *-- "1..*" Pin : 拥有引脚
Component <|-- MultiInputGate
MultiInputGate <|-- AndGate
MultiInputGate <|-- OrGate
MultiInputGate <|-- NandGate
MultiInputGate <|-- NorGate
MultiInputGate <|-- XorGate
Component <|-- NotGate
Component <|-- InputComponent
Component <|-- OutputComponent
Circuit *-- "0..*" WireConnection : 管理连接
WireConnection *-- "2" PinReference : 源端与目标端
PinReference --> "1" Component : 引用
Canvas ..> CircuitDocument : 文档与快照
MainFrame ..> CircuitFile : 文件命令
CircuitFile ..> CircuitDocument : 读写
CircuitFile ..> Circuit : 合法性校验
@enduml'''
codes['顺序图'] = '''@startuml
title 连接引脚并重新求值
skinparam monochrome true
actor 使用者 as User
boundary "画布交互 : Canvas" as UI
control "电路服务 : Circuit" as Circuit
entity "源元件 : Component" as Source
entity "目标元件 : Component" as Target
User -> UI : 选择源引脚与目标引脚
activate UI
UI -> Circuit : 请求连接(源引脚, 目标引脚)
activate Circuit
Circuit -> Source : 获取引脚方向及有效性
Source --> Circuit : 源端信息
Circuit -> Target : 获取引脚方向及有效性
Target --> Circuit : 目标端信息
Circuit -> Circuit : 检查重复、占用及反馈环路
alt 连接不合法
  Circuit --> UI : 连接失败及原因
  UI --> User : 显示拒绝原因
else 连接合法
  Circuit -> Circuit : 建立连接
  Circuit --> UI : 连接成功
  UI -> Circuit : 请求重新求值()
  Circuit -> Circuit : 建立拓扑关系并安排求值顺序
  loop 按拓扑顺序处理各元件
    Circuit -> Source : 求值并取得输出
    Source --> Circuit : 输出逻辑值
    Circuit -> Target : 传递逻辑值至后继输入
  end
  Circuit --> UI : 逻辑状态及求值结果
  UI -> UI : 更新修改状态与画布显示
  UI --> User : 显示线路与端口逻辑状态
end
deactivate Circuit
deactivate UI
@enduml'''

def find(text):
    return next(p for p in d.paragraphs if p.text == text)

def add_before(anchor, text, code=False):
    el = OxmlElement('w:p')
    anchor._p.addprevious(el)
    p = Paragraph(el, anchor._parent)
    p.paragraph_format.first_line_indent = Pt(0)
    p.paragraph_format.space_after = Pt(0 if code else 5)
    p.paragraph_format.space_before = Pt(0)
    p.paragraph_format.line_spacing = 1
    p.paragraph_format.keep_with_next = False
    r = p.add_run(text)
    r.font.size = Pt(9 if code else 10.5)
    r.font.name = 'Consolas' if code else '宋体'
    r._element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'), '宋体')
    if not code:
        r.bold = True
        p.paragraph_format.keep_with_next = True
    return p

def insert(anchor, name, note):
    add_before(anchor, name + ' PlantUML代码')
    p = add_before(anchor, note)
    p.runs[0].bold = False
    for line in codes[name].splitlines():
        add_before(anchor, line, True)
    add_before(anchor, '')

# Replace the earlier simplified pictures with the requested source blocks.
for p in list(d.paragraphs):
    if p._p.xpath('.//w:drawing') or p.text.startswith(('图3-1 ', '图3-2 ', '图3-3 ')):
        p._element.getparent().remove(p._element)

insert(find('3.1 系统组织设计'), '数据流图',
       '采用一级数据流图，箭头标注数据内容，D1为工作数据，D2为本地文件。图中存储符号表示逻辑数据存储，不表示引入数据库管理系统。')
# Insert package code immediately after the section heading.
head = find('3.2 系统结构设计')
next_p = Paragraph(head._p.getnext(), head._parent)
insert(next_p, '包图', '各包为拟定的逻辑职责分组，虚线箭头从使用方指向被依赖方，不表示独立部署单元。')
insert(find('3.2.3 设计类结构'), '用例结构图',
       '执行者位于系统边界之外，用例表示外部可见目标。包含关系箭头指向被包含的公共功能；连接成功后触发重新求值的过程见顺序图。')
insert(find('3.2.4 关键处理过程或者算法的设计'), '类图',
       '仅给出概要层次的主要职责和关系，操作签名在详细设计中确定。实心菱形位于拥有对象的一端，普通有向关联表示引用，不表示所有权。')
head = find('3.2.4 关键处理过程或者算法的设计')
next_p = Paragraph(head._p.getnext(), head._parent)
insert(next_p, '顺序图',
       '以连接引脚场景说明画布与电路模型的协作。画布和电路模型在本场景中分别承担边界与控制职责；循环中的源、目标元件为当前处理连接两端的角色。')
out = root.parent / 'report_filled_33_34.docx'
d.save(out)
assert len(d.tables) == 5
print(out)
