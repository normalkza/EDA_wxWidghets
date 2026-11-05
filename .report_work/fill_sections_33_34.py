from pathlib import Path
from docx import Document

src = Path("report_filled_33_34.docx")
dst = src
doc = Document(src)

# The template reserves one data row in the interface table. Fill that row in place.
table = doc.tables[2]
values = [
    "IF01\nIF02\nIF03",
    "人机交互接口\n文件存储接口\n模块协作接口",
    "用户接口\n文件接口\n内部模块接口",
    "编辑电路、打开/保存文档、传递文档模型与校验结果",
    "菜单、工具栏、画布、XML文件、CircuitDocument",
    "事件调用；UTF-8 XML；对象数据传递",
    "本节仅确定接口边界和交换信息；具体函数参数、事件绑定及异常分支在详细设计阶段确定。",
]
for cell, value in zip(table.rows[2].cells, values):
    cell.text = value

def add_before(anchor, text):
    p = anchor.insert_paragraph_before(text)
    p.style = doc.styles["Normal"]
    return p

heading_34 = next(p for p in doc.paragraphs if p.text.strip() == "3.4 系统完整性设计")
heading_4 = next(p for p in doc.paragraphs if p.text.strip() == "4 系统出错处理设计")

section_33 = [
    "本节在需求分析确定的功能边界和交互约束基础上，给出系统各类接口的职责、方向和交换信息。接口设计服务于概要结构划分，具体函数签名、控件事件绑定、错误码实现和线程处理方式在详细设计阶段进一步确定。",
    "3.3.2 系统接口传输协议说明",
    "人机交互接口采用wxWidgets事件机制。菜单、工具栏、工具箱和属性面板向MainFrame或Canvas发出编辑命令，Canvas返回选中对象、修改状态和显示结果；逻辑求值结果通过画布线路颜色、端口状态和状态栏信息反馈给使用者。接口传递的对象以当前电路文档为边界，避免界面控件直接修改文件内容。",
    "文件接口采用本地UTF-8 XML文档。打开时先读取到临时CircuitDocument，完成版本、元件、引脚、连线和文字记录的完整性校验后，再替换当前文档；保存时先写同目录临时文件，写入成功并通过校验后替换目标文件。文件失败时返回错误说明，当前画布和未保存状态保持不变。",
    "模块协作接口围绕CircuitDocument、Circuit、Canvas和CircuitFile展开。Canvas负责交互状态与显示，Circuit负责连接合法性检查和逻辑求值，CircuitFile负责文档读写与格式校验，CircuitDocument负责在界面、文件和撤销快照之间传递值数据。各模块通过明确的数据边界协作，详细设计阶段再确定具体调用顺序和参数对象。",
]
for text in reversed(section_33):
    add_before(heading_34, text)

section_34 = [
    "3.4 系统完整性设计",
    "本节在需求分析结果和系统结构设计基础上，规定概要层次的一致性、完整性和可恢复性约束，为详细设计阶段确定数据校验、状态转换和异常恢复方案提供依据。",
    "电路完整性由元件、引脚和连线共同保证。每个元件在文档内具有唯一标识，连线两端必须引用存在的引脚，并满足输出端到输入端的方向约束；同一输入端不允许重复驱动，不允许形成组合逻辑环路。删除元件或减少输入引脚时，应同步处理失效连线，避免文档留下悬空引用。",
    "文档完整性由CircuitDocument统一承载。components、wires和texts三类记录共同描述一个可编辑文档，逻辑求值结果属于可重算状态，不作为持久化数据的唯一依据。打开、导入和保存均以临时文档完成校验，校验通过后才提交到当前状态，防止部分数据覆盖有效文档。",
    "操作完整性由编辑状态和撤销快照保证。有效编辑应更新修改标志并形成可恢复快照，连续拖动等连续操作在概要层次视为一个编辑单元；新建、打开和恢复操作应同步更新画布、文档路径、修改标志及撤销边界。具体状态机、快照字段和边界处理在详细设计阶段确定。",
]
for text in reversed(section_34):
    add_before(heading_4, text)

doc.save(dst)
print(dst)
