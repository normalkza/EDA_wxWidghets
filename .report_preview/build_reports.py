from pathlib import Path
import re
from docx import Document
from docx.shared import Cm, Pt, RGBColor
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / '报告文档'
OUT.mkdir(exist_ok=True)
TMP = Path(__file__).resolve().parent

def font(run, size=11, bold=False):
    run.font.name = 'Times New Roman'
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.color.rgb = RGBColor(0, 0, 0)
    run._element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'), '宋体')

def base(title):
    d = Document()
    s = d.sections[0]
    s.page_width, s.page_height = Cm(21), Cm(29.7)
    s.top_margin, s.bottom_margin = Cm(2.2), Cm(2)
    s.left_margin, s.right_margin = Cm(2.2), Cm(2.2)
    for name in ['Normal', 'Title', 'Heading 1', 'Heading 2', 'Heading 3']:
        st = d.styles[name]
        st.font.name = 'Times New Roman'
        st.font.color.rgb = RGBColor(0, 0, 0)
        st._element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'), '宋体' if name == 'Normal' else '黑体')
    normal = d.styles['Normal']
    normal.font.size = Pt(11)
    normal.paragraph_format.line_spacing = 1.3
    normal.paragraph_format.space_after = Pt(6)
    for name, size in [('Title',22),('Heading 1',16),('Heading 2',13),('Heading 3',11.5)]:
        st=d.styles[name]; st.font.size=Pt(size)
        st.paragraph_format.space_before=Pt(12)
        st.paragraph_format.space_after=Pt(7)
    p=d.add_paragraph(title, 'Title'); p.alignment=WD_ALIGN_PARAGRAPH.CENTER
    foot=s.footer.paragraphs[0]; foot.alignment=WD_ALIGN_PARAGRAPH.CENTER
    r=foot.add_run(); fld=OxmlElement('w:fldSimple'); fld.set(qn('w:instr'),'PAGE'); r._r.addnext(fld)
    d.core_properties.title=title
    d.core_properties.subject='基于项目现有代码编写的课程报告'
    d.core_properties.author=''
    return d

def p(d,text):
    para=d.add_paragraph(text)
    para.paragraph_format.first_line_indent=Cm(.74)
    return para

def h(d,text,level=1):
    return d.add_heading(text,level)

def table(d, headers, rows, widths=None, size=9.5):
    t=d.add_table(rows=1, cols=len(headers)); t.alignment=WD_TABLE_ALIGNMENT.CENTER
    t.autofit=False
    if widths:
        for c,w in zip(t.columns,widths): c.width=Cm(w)
    for cell,text in zip(t.rows[0].cells,headers): cell.text=text
    for row in rows:
        for cell,text in zip(t.add_row().cells,row): cell.text=str(text)
    for i,row in enumerate(t.rows):
        pr=row._tr.get_or_add_trPr()
        if i==0:
            repeat=OxmlElement('w:tblHeader'); pr.append(repeat)
        cant=OxmlElement('w:cantSplit'); pr.append(cant)
        for j,cell in enumerate(row.cells):
            if widths: cell.width=Cm(widths[j])
            cell.vertical_alignment=WD_CELL_VERTICAL_ALIGNMENT.CENTER
            tcpr=cell._tc.get_or_add_tcPr()
            borders=OxmlElement('w:tcBorders')
            for edge in ['top','left','bottom','right']:
                e=OxmlElement('w:'+edge); e.set(qn('w:val'),'single'); e.set(qn('w:sz'),'4'); e.set(qn('w:color'),'D9D9D9'); borders.append(e)
            tcpr.append(borders)
            margins=OxmlElement('w:tcMar')
            for edge in ['top','left','bottom','right']:
                e=OxmlElement('w:'+edge); e.set(qn('w:w'),'85'); e.set(qn('w:type'),'dxa'); margins.append(e)
            tcpr.append(margins)
            if i==0:
                sh=OxmlElement('w:shd'); sh.set(qn('w:fill'),'E7E6E6'); tcpr.append(sh)
            for para in cell.paragraphs:
                para.paragraph_format.space_after=Pt(2); para.paragraph_format.space_before=Pt(2)
                para.paragraph_format.line_spacing=1.1
                if j==0: para.alignment=WD_ALIGN_PARAGRAPH.CENTER
                for r in para.runs: font(r,size,i==0)
    d.add_paragraph().paragraph_format.space_after=Pt(1)
    return t

def page(d): d.add_page_break()

def diagram(name, boxes, arrows, dimensions):
    im=Image.new('RGB',dimensions,'white'); draw=ImageDraw.Draw(im)
    f=ImageFont.truetype('C:/Windows/Fonts/simhei.ttf',26)
    for a,b in arrows:
        draw.line([a,b],fill='#555555',width=3)
        import math
        angle=math.atan2(b[1]-a[1],b[0]-a[0]); n=13
        draw.polygon([b,(b[0]-n*math.cos(angle-.45),b[1]-n*math.sin(angle-.45)),(b[0]-n*math.cos(angle+.45),b[1]-n*math.sin(angle+.45))],fill='#555555')
    for rect,text in boxes:
        draw.rectangle(rect,fill='#F3F3F3',outline='#555555',width=2)
        bb=draw.multiline_textbbox((0,0),text,font=f,spacing=8,align='center')
        x=(rect[0]+rect[2]-bb[2])/2; y=(rect[1]+rect[3]-(bb[3]-bb[1]))/2-bb[1]
        draw.multiline_text((x,y),text,font=f,fill='black',spacing=8,align='center')
    path=TMP/name; im.save(path); return path

def figure(d,path,caption):
    para=d.add_paragraph(); para.alignment=WD_ALIGN_PARAGRAPH.CENTER
    para.paragraph_format.keep_with_next=True
    para.add_run().add_picture(str(path),width=Cm(16))
    cp=d.add_paragraph(caption); cp.alignment=WD_ALIGN_PARAGRAPH.CENTER
    for r in cp.runs: font(r,10)

# First template: only the requested sections, using the already prepared text.
d=base('数字电路编辑与逻辑仿真系统课程项目报告')
p(d,'第一模板指定章节：第 1 节及第 3.2 节（含 3.2.1 至 3.2.6）。')
lines=(ROOT/'课程软件项目报告_第1节及3.2节.md').read_text(encoding='utf-8-sig').splitlines()
i=1
while i<len(lines):
    line=lines[i].strip(); i+=1
    if not line: continue
    if line.startswith('|'):
        block=[line]
        while i<len(lines) and lines[i].strip().startswith('|'):
            block.append(lines[i].strip()); i+=1
        rows=[[x.strip() for x in z.strip('|').split('|')] for z in block]
        table(d,rows[0],rows[2:],[2.5,5.4,8.7]); continue
    if line.startswith('### '): h(d,line[4:],2)
    elif line.startswith('## '): h(d,line[3:],1)
    else:
        p(d,line.replace('**',''))
first=OUT/'第一模板_第1节和3.2部分.docx'; d.save(first)

# Second template: complete outline and all required table fields.
d=base('数字电路编辑与逻辑仿真系统概要设计报告')
p(d,'本报告依据所提供的第二份软件概要设计报告模板，结合项目当前代码，完整说明系统结构、接口、异常处理、技术方案和文件存储设计，为课程评审、详细设计与后续维护提供依据。系统定位为本地数字电路编辑及二值组合逻辑仿真工具。')
table(d,['项目','内容'],[
    ('项目名称','数字电路编辑与逻辑仿真系统'),('文档版本','V1.0'),
    ('编制依据','项目 README、Visual Studio 工程及当前源代码'),
    ('编写与审核','编写人：待填写；审核人：待填写'),('提交信息','班级、学号、姓名及提交日期：待填写')],[3.2,13.4],10)
h(d,'目录',1)
for text in ['1 引言','2 设计概述','3 系统逻辑设计','4 系统出错处理设计','5 技术设计','6 数据库设计','7 词汇表','8 进度计划']:
    d.add_paragraph(text)
page(d)
h(d,'1 引言')
h(d,'1.1 编写目的',2)
p(d,'本报告将项目需求落实为可实现的模块、数据结构和接口，说明界面编辑、电路连接、信号计算与文档保存之间的协作关系，使开发人员能够据此细化实现，测试人员能够确定验证范围，课程评审人员能够核对设计与代码的一致性。')
p(d,'系统面向数字逻辑课程学习和课堂演示，支持与门、或门、非门、与非门、或非门、异或门以及输入输出端口。用户可以放置和连接元件、调整布局、切换输入电平并观察输出，也可以添加文字说明并保存电路文件。')
h(d,'1.2 项目风险',2)
table(d,['风险','可能影响','控制措施'],[
    ('连接关系不合法','产生悬空引用、多驱动或反馈环路','连接前统一校验；删除元件及减少引脚时清理失效连接。'),
    ('图形与模型不同步','画布显示与逻辑输出不一致','由画布统一提交修改，随后重算电路、刷新属性和显示。'),
    ('文件损坏或保存中断','无法恢复电路或覆盖原文件','读取后先完整校验；保存使用同目录临时文件，成功后替换目标。'),
    ('开发环境依赖','其他机器无法直接编译或运行','核对 v145 工具集、wxWidgets 路径和运行库，整理发布清单。'),
    ('范围扩展过快','增加时序元件后现有算法不适用','当前限定无反馈组合逻辑；时序仿真需另行设计。')],[3.2,4.2,9.2])
h(d,'1.3 预期读者和阅读建议',2)
p(d,'课程教师和评审人员可先阅读第 1、2、3 章，了解目标、范围和总体结构；开发人员重点阅读第 3、5、6 章，核对模块接口及数据约束；测试与维护人员重点阅读第 4、6、8 章，确定异常场景、文件兼容边界和后续工作。')
h(d,'1.4 参考资料',2)
table(d,['资料','用途'],[
    ('用户提供的《模板.doc》','作为本报告的章节及表格栏目依据。'),
    ('项目 README.md','核对功能、操作方式及回归测试说明。'),
    ('wx_demo.vcxproj 与 wx_demo.slnx','核对编译配置、代码文件及依赖。'),
    ('MainFrame、Canvas、Circuit、Component、CircuitFile 等源文件','核对窗口组织、模型接口、计算过程、文件结构和错误处理。'),
    ('tests/gate_regression.cpp、tests/storage_regression.cpp','了解现有回归用例范围；本报告编写未重新执行测试。')],[6,10.6])
page(d)
h(d,'2 设计概述')
h(d,'2.1 限制和约束',2)
p(d,'系统当前以 Windows 桌面程序形式运行，使用 C++20 和 wxWidgets，工程配置引用 v145 工具集及 D:\\wxWidgets-3.3.3。此绝对路径需要在其他开发机上调整。程序使用本地内存和 XML 文件，不依赖服务器、用户账户系统或关系数据库。')
p(d,'逻辑值仅包含低电平和高电平。非门固定为一个输入、一个输出；与门、或门、与非门、或非门和异或门支持 2 至 16 个输入。未连接输入按低电平参与计算；一个输入引脚仅允许一个驱动源，一个输出可以连接多个输入。导线交叉不自动构成电气连接。')
p(d,'当前仿真对象为无反馈的组合逻辑电路。系统不提供时钟、寄存器、存储器、模拟电路求解、传播延迟分析或 PCB 布线功能。图形位置和线路路径用于显示，实际逻辑连接由引脚引用确定。')
table(d,['资源项','现有校验上限'],[
    ('电路文件大小','8 MiB'),('元件、连线、文字框','分别为 1000、4000、1000 个'),
    ('连线路径点','单条最多 256 个；全部连线合计最多 4000 个'),
    ('坐标与文字框尺寸','坐标范围为 -1000000 至 1000000；宽高为 1 至 1000000'),
    ('文字与字号','单框最多 65536 字符，总计最多 262144 字符；字号 6 至 96 点'),
    ('撤销历史','最多 50 个文档快照')],[5,11.6])
p(d,'上述数值为实现中的输入与资源约束，不代表在所有设备上已达到相应规模的交互性能；性能结论仍需专门测试。')
h(d,'2.2 设计原则和设计要求',2)
p(d,'系统按职责划分界面、编辑、逻辑、连接和存储模块。逻辑门只处理自身输入输出关系，Circuit 维护连接并安排计算顺序，Canvas 处理坐标与交互，CircuitFile 负责文件解析及校验。界面通过事件和回调组织命令，避免在菜单或属性控件内复制核心规则。')
p(d,'设计要求包括：编辑结果与逻辑状态一致；非法连接不能进入有效电路；文件加载失败保留原画布；保存失败保留原文件；删除和参数调整不遗留失效引用；新增元件时同步扩展创建、显示、属性、复制和存储映射。对无法执行的操作应保留可恢复状态并给出反馈。')
page(d)
h(d,'3 系统逻辑设计')
h(d,'3.1 系统组织设计',2)
p(d,'系统是单进程桌面应用，所有功能部署于同一台用户计算机。以下编号为本报告使用的追踪标识，内部模块不作为独立可执行程序部署。默认生成程序名随工程配置确定，当前工程为 wx_demo。')
table(d,['子系统编号','英文名称','中文名称','业务职能','安装地点','备注'],[
    ('S01','wx_demo.exe','数字电路编辑器','电路编辑、计算、文档保存','用户 Windows 计算机','单机程序')],[1.6,2.8,2.8,4.6,2.5,2.3],9)
table(d,['模块','主要代码对象','职责'],[
    ('应用与窗口','MyApp、MainFrame','初始化窗口，组织菜单、工具栏、面板及文件操作。'),
    ('工具与属性','MenuBar、ToolBar、Toolbox、PropertyPanel、SelectionState','提供命令入口、工具选择及选中对象属性。'),
    ('画布编辑','Canvas、CanvasWiring、CanvasDocument','管理放置记录、交互、线路调整、文字、文档转换和撤销。'),
    ('元件模型','Component、MultiInputGate、各逻辑门及 IOComponent','保存引脚与参数，按元件规则计算输出。'),
    ('连接与计算','Circuit、PinReference、WireConnection','检查连接、维护有向关系并传播逻辑电平。'),
    ('线路几何','Wire','产生直角路径，绘制导线并进行点击判定。'),
    ('本地存储','CircuitDocument、CircuitFile','组织持久化数据，读写 XML 并验证完整性。')],[3.1,6.2,7.3])
org=diagram('module.png',[
    ((310,15,890,100),'用户操作  菜单  工具  属性'),
    ((310,155,890,245),'MainFrame 与 Canvas\n命令协调和画布编辑'),
    ((20,320,380,425),'Component 与 Circuit\n元件行为和逻辑传播'),
    ((420,320,780,425),'Wire\n路径与命中判定'),
    ((820,320,1180,425),'CircuitFile\nXML 校验与读写')],
    [((600,100),(600,155)),((440,245),(200,320)),((600,245),(600,320)),((760,245),(1000,320))],(1200,450))
figure(d,org,'图 3-1 系统内部模块关系')
page(d)
h(d,'3.2 系统结构设计',2)
h(d,'3.2.1 系统用例模块映射表',3)
p(d,'所属子系统为 S01，英文名称 wx_demo.exe，中文名称数字电路编辑器。UC 编号及英文用例名仅用于本报告追踪；“调用对象”表示发起处理的一方，“被调用对象”表示承接处理的模块。')
# Keep every template field, using a compact record table per use case for legibility.
cases=[
 ('UC01','EditComponent','编辑逻辑元件','放置、选择、移动、旋转和删除元件','MainFrame、工具面板','Canvas、Component、Circuit','元件旋转为 90 度的整数倍；删除时清理连接。'),
 ('UC02','ConfigurePins','设置元件参数','在属性面板调整可变输入门的输入数','PropertyPanel','Canvas、MultiInputGate、Circuit','输入数 2 至 16；移除不存在引脚的连接。'),
 ('UC03','ConnectPins','连接与调整导线','选择两端引脚，生成或调整直角路径','Canvas 连线交互','Circuit::Connect、Wire','检查方向、重复、输入占用和环路。'),
 ('UC04','EvaluateCircuit','计算并观察结果','切换输入电平，计算组合电路并刷新输出','Canvas、主窗口仿真命令','Circuit::Evaluate、Component::Evaluate','未连接输入为低电平；支持输出分支。'),
 ('UC05','EditTextAndUndo','文字编辑与撤销','添加及调整文字，复制粘贴对象，撤销修改','Canvas、编辑菜单','wxTextCtrl、剪贴板、CanvasDocument','最多 50 个文档快照；复制元件不复制外部连线。'),
 ('UC06','ManageDocument','管理电路文档','新建、打开、保存、另存为','MainFrame','Canvas 文档接口、CircuitFile','新建、打开和退出前检查未保存修改。')]
for case in cases:
    table(d,['用例编号与英文名称','用例中文名称','操作功能'],[(case[0]+'\n'+case[1],case[2],case[3])],[4.2,3.2,9.2])
    table(d,['调用对象','被调用对象','备注'],[(case[4],case[5],case[6])],[4.2,5.8,6.6])
h(d,'3.2.2 系统用例结构图',3)
p(d,'系统只有本地使用者这一类直接参与者。其操作覆盖元件编辑、连线、计算、文字和撤销、文档管理；这些用例由同一个主窗口提供入口。文档打开包含校验与导入，保存包含导出、校验与写入，计算依赖有效的元件和连接。')
uc=diagram('usecase.png',[
    ((15,150,205,270),'本地使用者'),((330,15,780,90),'元件编辑与参数设置'),
    ((330,115,780,190),'引脚连接与线路调整'),((330,215,780,290),'电平切换与逻辑计算'),
    ((330,315,780,390),'文字编辑  剪贴板  撤销'),((330,415,780,490),'新建  打开  保存文档')],
    [((205,210),(330,52)),((205,210),(330,152)),((205,210),(330,252)),((205,210),(330,352)),((205,210),(330,452))],(810,515))
figure(d,uc,'图 3-2 主要系统用例')
h(d,'设计类结构',3)
p(d,'Component 是抽象元件基类，包含名称、输入引脚集合和输出引脚集合，并声明 Evaluate 虚函数。MultiInputGate 继承 Component，统一管理多输入数量；与、或、与非、或非、异或门在此基础上实现运算。NotGate 采用固定单输入结构，输入输出端口提供电平来源或观察端。')
p(d,'Canvas 组合元件放置记录、Circuit 和文字框集合；放置记录维护元件所有权及几何属性。Circuit 持有 WireConnection 集合，连接通过 PinReference 引用元件与引脚，并不拥有被引用元件。CircuitDocument 使用可序列化数据和元件 ID 关联对象，CircuitFile 负责其创建、校验及读写。删除或重建模型时必须同步更新引用。')
table(d,['类或结构','关键数据或接口','关系'],[
    ('Component','name、inputs、outputs、Evaluate()','逻辑元件抽象基类'),
    ('MultiInputGate','SetInputCount()，范围 2 至 16','由多输入逻辑门派生使用'),
    ('PinReference','component、type、index','非拥有引用，用于定位引脚'),
    ('WireConnection','output、input、route','描述一条有向连接及可选路径'),
    ('Circuit','Connect()、Evaluate()、PruneInvalidConnections()','管理连接集合与传播顺序'),
    ('CircuitDocument','components、wires、texts','画布与文件之间的数据载体')],[3.7,7.5,5.4])
page(d)
h(d,'3.3 系统接口设计',2)
h(d,'3.3.1 系统接口表',3)
p(d,'系统接口以进程内调用、本地文件和操作系统剪贴板为主。以下接口均不使用网络传输；“接口速率”不适用，以同步返回或事件触发描述执行方式。')
interfaces=[
 ('IF01','用户操作接口','人机','输入','事件触发','wxWidgets 事件','MainFrame、Canvas','鼠标及键盘命令'),
 ('IF02','元件运算接口','内部','双向','同步调用','C++ 虚函数 Evaluate','Circuit、Component','读取输入，更新输出'),
 ('IF03','连接管理接口','内部','双向','同步调用','Connect 与 ConnectionResult','Canvas、Circuit','返回连接处理结果'),
 ('IF04','文档转换接口','内部','双向','同步调用','ExportDocument / ImportDocument','Canvas、CircuitDocument','交换结构化文档'),
 ('IF05','电路文件接口','文件','双向','不适用','UTF-8 XML version 1','CircuitFile、本地磁盘','Load / Save 返回 bool 及 error'),
 ('IF06','系统剪贴板接口','操作系统','双向','不适用','wx_demo.canvas.object.v1','Canvas、wxClipboard','对象格式；文字兼容纯文本')]
for r in interfaces:
    table(d,['接口编号','接口名称','接口类型','接口性质'],[(r[0],r[1],r[2],r[3])],[2,6.6,4,4])
    table(d,['接口速率','接口协议','相关对象','备注'],[(r[4],r[5],r[6],r[7])],[2.7,5.1,4.6,4.2])
h(d,'3.3.2 系统接口传输协议说明',3)
p(d,'内部接口在同一进程中同步传递对象引用、容器或文档结构，不定义字节传输速率。Circuit::Connect 返回 Added、Duplicate、InvalidPin、SameDirection、InputOccupied 或 Cycle。画布据此决定保留连接或提示失败原因。Circuit::Evaluate 接收元件指针集合，计算各元件并返回处理是否覆盖全部节点。')
p(d,'文件接口采用 XML 1.0 与 UTF-8 编码，根节点为 circuit，version 为 1。components、wires、texts 分别保存元件、线路和文字；线路以元件 ID 和零起始的引脚索引引用两端。Load、Save、Validate 通过布尔值和 error 字符串传递结果。失败不得被当作成功提交。')
p(d,'剪贴板自定义格式名为 wx_demo.canvas.object.v1，数据以 WX_DEMO_1 标识开头，再区分 COMPONENT 和 TEXT。元件内容包括类型、位置、输入输出状态和旋转角度；文字内容包括矩形、字号、滚动状态及 UTF-8 文字。复制文字同时提供普通文本格式；粘贴时验证标识与参数，拒绝不合法载荷。')
page(d)
h(d,'3.4 系统完整性设计',2)
p(d,'电路完整性以单输入单驱动、输出到输入的方向约束及无反馈为基础。Connect 在添加连接前检查引脚和已有关系；若从拟连接目标能够到达源元件，则新增连接会构成环路，应拒绝添加。删除元件时删除关联线路，减少引脚时清理越界引用，计算前也清理失效连接。')
p(d,'文件完整性分为语法和语义两层。语法检查节点、属性、整数、编码和版本；语义检查元件类型、ID 唯一性、引脚数量、引用存在性、连接重复、多驱动与环路。导入采用先构建后替换，解析或模型构建失败时保留当前画布。保存采用先校验、序列化、写同目录临时文件、刷新并关闭、替换目标的顺序；替换失败时不先删除原文件。')
p(d,'操作完整性通过修改标记、未保存确认和撤销快照保持。新建、打开、退出前允许保存、放弃或取消；剪切先确认复制成功再删除对象。撤销恢复文档快照，重建失败时保留当前模型与历史。当前未设计多用户并发写入或文件版本合并功能。')
h(d,'部署设计',3)
p(d,'部署对象为单台 Windows 计算机，程序、所需 wxWidgets 动态库或相应运行依赖以及资源目录共同组成运行环境。工程含构建后资源复制步骤，发布时应核对可执行文件同级的 res 目录和实际资源路径。应用通过本地读写权限访问电路文件，不需安装数据库或启动后台服务。发布包应在未配置开发环境的目标机器上验证，不能仅以开发机可运行为验收依据。')
h(d,'关键处理过程或者算法的设计',3)
p(d,'逻辑计算采用拓扑顺序传播。首先根据当前有效元件及连线建立节点表、入度和出边关系，并将输入引脚初始化为低电平；将入度为零的元件加入队列。每次取出元件后调用 Evaluate，将输出传给下游输入并减少后继入度；后继入度为零时入队。队列结束后，若已处理元件数等于节点总数则成功，否则存在未完成的依赖关系。按哈希访问平均常数时间估计，计算过程时间复杂度为 O(V+E)，空间复杂度为 O(V+E)。')
p(d,'多输入与门要求全部输入为高，或门要求至少一个输入为高；与非和或非分别对上述结果取反；异或门按高电平输入的奇偶性输出，奇数为高；非门翻转单个输入。此计算只反映稳定的二值结果，不模拟门延迟和竞争冒险。')
flow=diagram('flow.png',[
    ((20,35,275,130),'整理有效元件\n建立入度与出边'),((330,35,585,130),'零入度节点入队'),
    ((640,35,895,130),'出队并计算元件'),((640,205,895,300),'传播输出\n更新后继入度'),
    ((330,205,585,300),'队列非空则继续\n为空则检查计数'),((20,205,275,300),'全部节点已处理\n返回计算结果')],
    [((275,82),(330,82)),((585,82),(640,82)),((767,130),(767,205)),((640,252),(585,252)),((330,252),(275,252))],(925,330))
figure(d,flow,'图 3-3 逻辑传播的主要处理流程')
page(d)
h(d,'4 系统出错处理设计')
h(d,'4.1 系统出错处理表',2)
p(d,'E 编号为报告追踪标识。连接类错误采用代码中的枚举结果；文件类错误列出已有提示示例或含义，不增设代码中不存在的错误码。所有失败处理均应明确是否已经提交修改。')
errors=[
 ('E01','无效引脚','对象不存在或索引越界','InvalidPin','拒绝连接并清理失效引用','交互或导入'),
 ('E02','连接方向错误','两端均为输入或均为输出','SameDirection','拒绝连接','原连接不变'),
 ('E03','重复连接','相同两端已连接','Duplicate','不重复添加','原连接不变'),
 ('E04','输入已占用','输入已有其他驱动源','InputOccupied','拒绝新增连接','输出分支允许'),
 ('E05','反馈环路','新增边形成有向环','Cycle','拒绝新增连接','组合逻辑限制'),
 ('E06','文件结构或参数无效','版本、类型、ID 或引用不合法','返回 false，error 说明原因','中止加载并保留当前画布','先校验后提交'),
 ('E07','文件资源超限','文件或对象数量超过阈值','如“元件数量超过上限（1000）。”','拒绝解析或导入','限制见第 2 章'),
 ('E08','保存失败','无写权限、空间不足或目标占用','返回 false，error 说明原因','保留原文件，允许改路径重试','不清除修改状态'),
 ('E09','剪贴板无效或不可用','格式损坏、类型不支持或无法锁定','复制或粘贴返回 false','不删除原对象，不插入无效对象','剪切先复制'),
 ('E10','撤销重建失败','快照无法构建为有效模型','状态栏报告 error','保留当前画布和撤销历史','等待用户后续处理')]
table(d,['错误编号','错误名称','错误原因','错误信息','处理方式','备注'],errors,[1.3,2.1,3,3.5,4.3,2.4],9)
h(d,'4.2 维护处理过程表',2)
p(d,'维护处理主要依靠现有校验、重算及文件保护机制；当前代码未提供独立的运维后台或集中日志系统。下表中的英文名称使用已有接口或内部处理过程名。')
maintenance=[
 ('E01—E05','Circuit::Connect','校验新增连接','验证端点、方向及拓扑关系','两个 PinReference','ConnectionResult','通过后才修改连接集合'),
 ('E01','PruneInvalidConnections','清理失效连接','移除无效引脚关系','当前连接集合','清理后的集合','元件或引脚变动后执行'),
 ('E06—E07','CircuitFile::Load / Validate','加载与校验文档','解析文件并检查数据约束','path 或 document，error','bool；文档或错误说明','加载失败不替换输出文档'),
 ('E08','CircuitFile::Save','安全保存文档','校验、临时写入及替换','path、document、error','bool，error','失败保留原目标文件'),
 ('E06、E10','Canvas::ReplaceDocument','重建画布模型','先构建完整模型再提交','document、error、resetHistory','bool，error','导入和撤销的恢复基础'),
 ('E09','CopySelection / PasteSelection','剪贴板处理','编码或验证对象载荷','当前选择或剪贴板数据','bool','剪切在复制成功后删除')]
for r in maintenance:
    table(d,['错误编号','处理过程英文名称','处理过程中文名称'],[(r[0],r[1],r[2])],[2.3,9.3,5])
    table(d,['处理功能','入口参数','出口参数','备注'],[(r[3],r[4],r[5],r[6])],[4.7,4.2,3,4.7])
page(d)
h(d,'5 技术设计')
h(d,'5.1 系统开发技术说明表',2)
table(d,['技术或工具','项目中的使用方式','说明'],[
    ('C++20','定义类、容器、算法及对象所有权','工程指定 stdcpp20。'),
    ('Visual Studio 与 MSBuild','组织 Debug / Release、Win32 / x64 配置','当前工具集为 v145；具体可用环境需核对。'),
    ('wxWidgets 3.3.3','窗口、控件、事件、绘图、XML、剪贴板','当前工程引用本地 wxwidgets.props 及库目录。'),
    ('面向对象与多态','统一 Component 接口，派生不同逻辑门','运算逻辑可分别验证。'),
    ('有向图及拓扑计算','连接校验和组合逻辑传播','禁止反馈，按依赖关系求值。'),
    ('XML 本地文件','保存元件、连线、文字和路径','UTF-8、版本 1，统一验证。'),
    ('文档快照','支持编辑撤销','历史数量限制为 50。'),
    ('PowerShell 回归入口','运行门逻辑与存储回归程序','对应 tests 目录中的两个运行脚本。')],[3.8,7.5,5.3])
h(d,'5.2 开发技术应用说明',2)
p(d,'界面采用 wxWidgets 事件驱动方式。主窗口组织工具和文件命令，画布处理鼠标与键盘操作，根据选择状态更新属性面板。几何绘制与逻辑连接分别保存，使元件移动、旋转和导线路径调整不改变逻辑关系。网格间距为 20，吸附仅约束布局操作。')
p(d,'元件采用统一抽象接口，各逻辑门独立实现 Evaluate。可变输入门集中验证参数，避免各派生类重复处理输入数量。连接使用带方向的引脚引用，持久化时转换为 ID 与索引；对象删除和文档重建时清理或重建引用，防止将内存地址写入文件。')
p(d,'存储模块使用 wxXmlDocument 读写 XML，通过集中 Validate 检查数据。写入先在内存中序列化，再通过临时文件提交，降低部分写入损坏旧文件的风险。撤销保存的是可序列化文档状态，恢复路径复用文档重建逻辑。')
p(d,'现有测试代码覆盖逻辑门真值表、输入数边界、连接关系、传播、几何交互、复制和 XML 存储等场景。后续验收应实际运行 tests/run_regression.ps1 与 tests/run_storage_regression.ps1，并记录环境、结果与失败用例。本报告不将已有测试文件等同于测试已经通过。')
page(d)
h(d,'6 数据库设计')
p(d,'本项目未使用关系数据库，也未设置数据表、SQL 或数据库账户。按照模板对自定义文件存储的要求，本章说明电路 XML 文件及其内存文档结构。一个文件保存一个电路，CircuitDocument 为数据交换单位，包含元件、连线和文字集合。')
h(d,'6.1 存储结构与数据关系',2)
p(d,'根节点 circuit 的 version 属性固定为 1，下设 components、wires、texts。component 的 id 在当前文件内唯一；wire 的 sourceId 与 targetId 分别引用源和目标元件，sourcePin 与 targetPin 为零起始引脚索引。文字框与元件没有外键关系，其坐标只表示布局。运行时计算得到的普通门输出不作为独立历史结果表保存。')
table(d,['数据对象','字段','含义及约束'],[
    ('component','id、type','正整数唯一标识；type 必须为实现支持的元件类型。'),
    ('component','x、y、rotation','位置坐标；角度为 0、90、180、270，缺省为 0。'),
    ('component','inputCount、high','输入数量必须匹配元件类型；high 仅允许 0 或 1，用于端口状态。'),
    ('wire','sourceId、sourcePin','源元件标识与输出引脚索引，必须存在且有效。'),
    ('wire','targetId、targetPin','目标元件标识与输入引脚索引，必须存在且有效。'),
    ('wire/point','x、y','可选手动路径；相邻点形成非零水平或垂直线段。'),
    ('text','x、y、width、height、pointSize','文字框矩形及字号；范围见第 2 章。'),
    ('text 节点内容','Unicode 文字','支持多行；写入时按 XML 规则转义。')],[3,5.4,8.2])
h(d,'6.2 文件示例',2)
p(d,'以下示例用一个高电平输入端口连接一个输出端口，文字框用于说明电路。此示例展示节点关系，不包含手动路径；无路径时由程序生成显示路径。')
xml='''<?xml version="1.0" encoding="UTF-8"?>
<circuit version="1">
  <components>
    <component id="1" type="INPUT" x="100" y="100"
               inputCount="0" high="1"/>
    <component id="2" type="OUTPUT" x="300" y="100"
               inputCount="1" high="0"/>
  </components>
  <wires>
    <wire sourceId="1" sourcePin="0"
          targetId="2" targetPin="0"/>
  </wires>
  <texts>
    <text x="80" y="200" width="280" height="80"
          pointSize="14">输入端口连接输出端口</text>
  </texts>
</circuit>'''
para=d.add_paragraph()
para.paragraph_format.line_spacing=1.0
for j,line in enumerate(xml.splitlines()):
    r=para.add_run(line+('\n' if j<len(xml.splitlines())-1 else '')); font(r,9)
    r.font.name='Consolas'
h(d,'6.3 一致性与兼容性要求',2)
p(d,'加载过程先验证 XML 声明、版本、结构、节点数量及属性，再验证元件参数和连接关系，最后构建新模型并提交。重复 ID、未知元件、非法引脚、多驱动、重复连接和环路都应被拒绝。保存前也运行相同文档校验，避免仅在读取端防护。')
p(d,'版本 1 中 rotation 可以省略，按 0 度处理；wire 可以不含 point，恢复为自动线路。修改格式时必须明确版本处理和旧文件默认值。持久化数据不保存临时选中状态、鼠标拖拽状态或撤销历史；打开后应依据文档重新构建对象并计算当前电平。')
page(d)
h(d,'7 词汇表')
table(d,['术语','定义'],[
    ('逻辑门','根据输入二值状态执行逻辑运算的元件。'),
    ('组合逻辑','输出由当前输入决定，不依赖历史存储状态的逻辑结构。'),
    ('高电平与低电平','本系统以 LogicValue 表示的 1 和 0，不对应实际电压数值。'),
    ('引脚','元件对外接收或输出逻辑值的端点，区分输入和输出。'),
    ('单驱动与扇出','一个输入仅接受一个源；一个输出可以驱动多个输入。'),
    ('反馈环路','沿有向连接能够回到原元件的闭合依赖关系。'),
    ('拓扑顺序','使前驱元件先于后继处理的无环有向图顺序。'),
    ('直角走线','由水平和垂直线段组成的导线显示路径。'),
    ('文档快照','某个时刻的 CircuitDocument，用于恢复编辑前状态。'),
    ('XML 与 UTF-8','分别指结构化文件标记格式和本项目使用的文字编码。'),
    ('接口与回调','模块之间约定的调用方式，以及事件发生后调用的处理函数。'),
    ('EDA','电子设计自动化；本项目仅实现其中基础数字电路编辑与逻辑演示功能。')],[4,12.6])
h(d,'8 进度计划')
p(d,'下表是课程提交前的建议安排，采用相对周次，不能作为已经发生的开发记录。当前仓库已存在主要功能与回归代码，实际完成日期、成员姓名和测试结论应由项目组据真实记录补充。若课程周期不同，可合并阶段并保留验收项。')
table(d,['阶段','任务与涉及模块','人员安排','产出与验收'],[
    ('第 1 周','核对功能范围与需求，整理总体结构','总体设计负责人，姓名待填','确认功能清单、边界和模块关系。'),
    ('第 2 周','核对主窗口、元件、属性及画布编辑','界面与编辑负责人，姓名待填','检查放置、移动、旋转、参数、文字和撤销。'),
    ('第 3 周','核对连接校验与逻辑传播','逻辑负责人，姓名待填','验证真值表、分支、多级传播及非法连接。'),
    ('第 4 周','核对 XML 存储和异常保护','存储负责人，姓名待填','验证往返保存、损坏文件、加载回退与保存失败。'),
    ('第 5 周','执行回归、整理缺陷和目标机部署','测试负责人及各模块负责人，姓名待填','记录环境和结果，修复并复测缺陷。'),
    ('第 6 周','报告定稿与课程演示准备','报告负责人及全体成员，姓名待填','核对报告、代码和演示一致，补齐署名及日期。')],[2,6.3,3.7,4.6])
p(d,'规模较小的小组可由同一成员承担多个职责，但每项验收应明确责任人。影响逻辑正确性或数据完整性的缺陷应优先处理；完成测试和部署验证后，再填写最终验收结论。')
second=OUT/'第二模板_完整概要设计报告.docx'; d.save(second)

# Structural checks ensure delivery scope and document package integrity.
for path in [first,second]:
    check=Document(path)
    assert check.paragraphs and check.tables
    print(str(path),path.stat().st_size)
full='\n'.join(x.text for x in Document(second).paragraphs)
for term in ['1.1 编写目的','3.2.1 系统用例模块映射表','3.3.2 系统接口传输协议说明','4.2 维护处理过程表','5.2 开发技术应用说明','6 数据库设计','7 词汇表','8 进度计划']:
    assert term in full,term
