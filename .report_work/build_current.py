from pathlib import Path
from copy import deepcopy
import hashlib, json
from docx import Document
from docx.text.paragraph import Paragraph
from docx.table import Table
from docx.shared import Pt, Inches
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parent.parent
TMP=Path(__file__).resolve().parent
REF=TMP/'current_reference.docx'
OUT=ROOT/'报告文档'/'数字电路编辑与逻辑仿真系统_概要设计报告.docx'
d=Document(REF)
original_paras=list(d.paragraphs)
templates={k:deepcopy(next(p._p for p in original_paras if p.style.name==k)) for k in ['Normal','Heading 1','Heading 2','Heading 3']}
tables=[deepcopy(t._tbl) for t in d.tables]
inventory={}
import zipfile
with zipfile.ZipFile(REF) as z:
    inventory={n:hashlib.sha256(z.read(n)).hexdigest() for n in z.namelist() if n!='word/document.xml'}
(TMP/'artifact.md').write_text('''# 模板执行约定
参考：current_reference.docx，由用户原模板.doc只读转换；原文件保持不变。
页面：保留模板节属性、页眉页脚、样式、编号和关系。正文采用模板Normal，标题采用Heading 1至3。
原模板字体：正文10.5磅，一级14磅，二级12磅；原有段落缩进及行距继承。
表格：保留原有五表的列序、合并单元格和边框；按数据量复制空白行，不增设表格位置。
槽位：引言、设计概述、逻辑设计、出错处理、数据库、词汇和计划替换说明文字。
3.3和3.4仅保留标题及空白接口表；原正文“部署设计”对应目录3.4，按目录名称保留。
目录中的技术设计章补齐，5.1因原模板无实际表格而以段落书写。
原有示例成绩系统插图删除，改为本项目结构及流程图。正文长度允许自然增页。
保留包部件清单见template_parts.json；只允许word/document.xml变化。
''',encoding='utf-8')
(TMP/'template_parts.json').write_text(json.dumps(inventory,indent=2),encoding='utf-8')
body=d._element.body
sect=deepcopy(body.sectPr)
for el in list(body): body.remove(el)
body.append(sect)

def para(text='',style='Normal'):
    el=deepcopy(templates[style])
    for c in list(el):
        if c.tag!=qn('w:pPr'): el.remove(c)
    pr=el.find(qn('w:pPr'))
    if pr is not None:
        for n in list(pr):
            if n.tag in [qn('w:sectPr'),qn('w:numPr'),qn('w:pageBreakBefore')]: pr.remove(n)
    body.insert(len(body)-1,el)
    p=Paragraph(el,d._body)
    if style.startswith('Heading'):
        num=OxmlElement('w:numPr'); nid=OxmlElement('w:numId'); nid.set(qn('w:val'),'0'); num.append(nid)
        p._p.get_or_add_pPr().append(num)
    p.add_run(text)
    return p
def h(t,l=1):
    p=para(t,'Heading '+str(l))
    p.paragraph_format.keep_with_next=True
    return p
def p(t): return para(t)
def table(index,rows,note=''):
    el=deepcopy(tables[index]); body.insert(len(body)-1,el); t=Table(el,d._body)
    if index==0:
        proto=deepcopy(t.rows[1]._tr)
        for r in list(t.rows)[1:]: t._tbl.remove(r._tr)
        for values in rows:
            t._tbl.append(deepcopy(proto))
            for c,text in zip(t.rows[-1].cells,values): c.text=text
    else:
        data_i=3 if index==4 else 2
        proto=deepcopy(t.rows[data_i]._tr)
        end=deepcopy(t.rows[-1]._tr)
        for r in list(t.rows)[data_i:]: t._tbl.remove(r._tr)
        t.rows[0].cells[0].text='子系统编号：S01\n子系统英文名称：wx_demo.exe\n子系统中文名称：数字电路编辑与逻辑仿真系统'
        for values in rows:
            t._tbl.append(deepcopy(proto))
            for c,text in zip(t.rows[-1].cells,values): c.text=text
        t._tbl.append(end); t.rows[-1].cells[0].text='说明：'+note
    for ri,row in enumerate(t.rows):
        trpr=row._tr.get_or_add_trPr()
        cant=OxmlElement('w:cantSplit'); trpr.append(cant)
        if (index==0 and ri==0) or (index!=0 and ri<(3 if index==4 else 2)):
            repeat=OxmlElement('w:tblHeader'); trpr.append(repeat)
        for n in list(trpr):
            if n.tag==qn('w:trHeight'): trpr.remove(n)
        seen=set()
        for c in row.cells:
            if id(c._tc) in seen: continue
            seen.add(id(c._tc))
            for pa in c.paragraphs:
                pa.paragraph_format.keep_with_next=False
                pa.paragraph_format.space_after=Pt(2)
                pa.paragraph_format.space_before=Pt(2)
                for r in pa.runs:
                    r.font.size=Pt(9)
                    r.font.name='Times New Roman'
                    r._r.get_or_add_rPr().rFonts.set(qn('w:eastAsia'),'宋体')
    return t

fontpath='C:/Windows/Fonts/msyh.ttc'
def diagram(name,boxes,edges,size=(1300,650)):
    im=Image.new('RGB',size,'white'); dr=ImageDraw.Draw(im); f=ImageFont.truetype(fontpath,22)
    for a,b in edges:
        x,y,w,hh,_=boxes[a]; xx,yy,ww,hhh,_=boxes[b]
        start=(x+w//2,y+hh); end=(xx+ww//2,yy)
        if end[1]<=start[1]: start=(x+w,y+hh//2); end=(xx,yy+hhh//2)
        dr.line([start,end],fill='black',width=2)
        ex,ey=end; sx,sy=start
        import math
        ang=math.atan2(ey-sy,ex-sx)
        dr.polygon([end,(ex-13*math.cos(ang-.4),ey-13*math.sin(ang-.4)),(ex-13*math.cos(ang+.4),ey-13*math.sin(ang+.4))],fill='black')
    for x,y,w,hh,text in boxes:
        dr.rectangle((x,y,x+w,y+hh),fill='white',outline='black',width=2)
        dr.multiline_text((x+w/2,y+hh/2),text,font=f,fill='black',anchor='mm',align='center',spacing=8)
    path=TMP/name; im.save(path)
    pa=para(); pa.paragraph_format.first_line_indent=0
    pa.add_run().add_picture(str(path),width=Inches(5.65))

title=para('数字电路编辑与逻辑仿真系统\n概要设计报告'); title.alignment=1
title.runs[0].bold=True; title.runs[0].font.size=Pt(18)
p('项目名称：EDA_wxWidghets    文档版本：1.0')
p('编制日期：2026年10月28日')
para().add_run().add_break(__import__('docx').enum.text.WD_BREAK.PAGE)
directory=para('目录'); directory.runs[0].bold=True; directory.runs[0].font.size=Pt(14)
tocp=para()
fld=OxmlElement('w:fldSimple'); fld.set(qn('w:instr'),'TOC \\o "1-3" \\h \\z \\u'); tocp._p.append(fld)
para().add_run().add_break(__import__('docx').enum.text.WD_BREAK.PAGE)
h('1 引言')
p('本报告说明数字电路编辑与逻辑仿真系统的总体结构、模块职责、主要数据、关键处理流程和错误恢复方式，为后续维护、功能扩展与测试提供统一的设计依据。系统面向组合逻辑电路的绘制与验证，采用本地桌面交互方式运行。')
h('1.1 编写目的',2)
p('本报告依据项目README、Visual Studio工程配置及现有源代码编写，将已经实现的功能整理为可追溯的概要设计。系统目标是让使用者在画布中放置逻辑门和输入输出端口，通过引脚连线建立电路，切换输入观察输出，并保存和重新打开电路文件。')
p('报告范围覆盖单窗口电路编辑程序的界面组织、元件模型、连线关系、逻辑求值、文字注释、编辑撤销和XML文档管理。时钟、存储器、复用器等后续元件不计入当前实现范围。')
h('1.2 项目风险',2)
p('任务提出者承担需求边界和验收口径风险，应明确本阶段以二值组合逻辑为目标，避免把时序电路、工业EDA文件互操作等功能纳入现有成果。')
p('软件开发者承担模型与绘制一致性、无效引脚引用、反馈环路、文件兼容及运行环境配置风险。旋转、改变输入数和删除元件后必须同步修正线路；文件读取应先校验再替换画布；构建时须核对wxWidgets库与工程平台配置。')
p('产品使用者承担错误连线、误删及未保存退出的风险。系统通过连线合法性检查、最多50步撤销、修改标识和退出保存询问降低损失；重要电路仍应保留独立备份。项目负责人承担进度与人员安排风险，应优先保障现有功能回归，再推进扩展元件。')
h('1.3 预期读者和阅读建议',2)
p('使用者宜先阅读第1、2章了解功能范围，再阅读第7章术语。开发人员应重点阅读第3章模块结构和关键算法、第4章错误恢复以及第5、6章技术与数据设计。测试人员应结合第2章边界、第3章用例映射和第4章错误清单设计验证场景。项目负责人和文档维护人员应阅读第1章风险及第8章计划，并结合源码维护功能描述。')
h('1.4 参考资料',2)
for x in ['[1] 项目README.md，项目维护者，当前工作区版本，项目根目录。描述元件参数、连线规则、文件格式与回归入口。','[2] wx_demo.vcxproj及wx_demo.slnx，项目维护者，当前工作区版本，项目根目录。提供C++标准、平台工具集和wxWidgets依赖配置。','[3] MainFrame、Canvas、Toolbox、PropertyPanel、MenuBar、ToolBar源文件，项目维护者，当前工作区版本，wx_demo目录。提供界面和交互设计依据。','[4] Component、Pin、LogicValue、各逻辑门、Circuit和Wire源文件，项目维护者，当前工作区版本，wx_demo目录。提供电路模型和算法依据。','[5] CircuitFile与CanvasDocument源文件，项目维护者，当前工作区版本，wx_demo目录。提供持久化、导入及撤销依据。','[6] gate_regression.cpp、storage_regression.cpp及配套PowerShell脚本，项目维护者，当前工作区版本，tests目录。提供回归验证范围。','[7] 原模板.doc，用户提供，2026年10月28日接收。提供本报告章节结构、填写要求及表格格式。']: p(x)
h('2 设计概述')
p('系统采用事件驱动的桌面应用结构。界面接收操作，画布管理编辑状态，电路模型校验连接并进行逻辑计算，文件模块负责结构化数据的校验与读写。各模块共同形成“编辑—求值—显示—保存”的工作闭环。')
h('2.1 限制和约束',2)
p('开发工程采用C++20、MSVC平台工具集v145，工程文件引用D:\\wxWidgets-3.3.3下的属性文件、头文件和库。当前运行与验证以Windows和项目所需Visual Studio环境为准；更换机器时应调整依赖路径，不能仅凭wxWidgets的跨平台特性认定工程已经完成跨平台验证。res资源目录应与运行程序位于同一目录。')
p('模型使用Low和High表示0与1，不模拟电压、电流、传播延迟、未知态及高阻态。支持AND、OR、NOT、NAND、NOR、XOR与INPUT、OUTPUT；多输入门允许2～16个输入，NOT固定一个输入。未连接输入按0处理，同一输入只能由一个输出驱动，输出允许分支，组合逻辑反馈连接被拒绝。')
p('文档采用版本1的UTF-8 XML，大小上限8 MiB，最多1000个元件、4000条线路和1000个文字框。坐标绝对值不超过1000000，文字框宽高为1～1000000，字号6～96；单框最多65536字符，文字总量最多262144字符。单条手动线路最多256个路径点，全部线路手动路径点合计最多4000个。')
p('当前项目资料未给出固定资金预算、人员名单和交付日期；本报告第8章按相对工作日提出维护与收尾安排，实际排期由项目负责人确认。系统目标是在上述范围内保证电路连接有效、显示与逻辑一致、文档可恢复和常用编辑操作可撤销。')
h('2.2 设计原则和设计要求',2)
p('命名沿用项目现有规则：类型采用MainFrame、CircuitDocument等名称，成员变量使用m_前缀，常量表达资源边界，操作使用Connect、Evaluate、Load、Save等动词。文档编号用于报告追踪，不替代代码中的类型与函数名。')
p('模块按职责划分。MainFrame负责窗口和文档命令，Canvas负责交互与几何状态，Component及其子类负责逻辑行为，Circuit负责拓扑和传播，Wire负责路径及命中，CircuitFile负责文件校验与存储。界面通过回调同步属性和状态，避免把门逻辑分散到控件事件中。')
p('数据边界采取显式校验：引脚必须存在、连线必须方向互补、输入不得多驱动、拓扑不得成环。导入数据必须通过统一验证后才提交到当前画布，保存使用临时文件完成写入后替换目标。系统以本地文件权限管理访问，未实现用户账户、网络认证或文件加密。')
p('交互要求提供一致的工具选择、网格显示与吸附、属性联动、旋转、文字编辑和错误提示。维护要求保留模型与界面的分工，并利用现有回归脚本覆盖门真值表、参数边界、图形交互和文件往返。新增元件应同时补齐创建、绘制、属性、求值和序列化处理。')
h('3 系统逻辑设计')
p('系统逻辑模型由元件、引脚、导线和文字构成。元件通过有向连接形成无环组合逻辑网络，画布位置和旋转只影响展示，连接关系决定逻辑传播。持久化文档以元件ID重建连接，不保存内存地址。')
h('3.1 系统组织设计',2)
p('本系统以wx_demo.exe作为一个本地运行单元交付。内部模块均编译到同一程序，不作为独立安装的子系统。')
table(0,[['S01','wx_demo.exe','数字电路编辑与逻辑仿真系统','绘制电路、编辑参数与文字、连接引脚、组合逻辑求值、XML存取','使用者的Windows本地计算机','单进程桌面程序；res资源随程序部署']])
h('3.2 系统结构设计',2)
p('界面组织层由MyApp启动MainFrame，主窗口组合菜单、工具栏、元件库、属性栏和Canvas。编辑层由Canvas.cpp、CanvasWiring.cpp和CanvasDocument.cpp协作实现；三者共同实现同一个Canvas类。模型层包含Component、各门类、Pin、Circuit和Wire。文件层由CircuitDocument数据结构与CircuitFile实现。')
diagram('current_structure.png',[(405,15,490,80,'MyApp → MainFrame\n窗口与命令协调'),(25,165,360,85,'MenuBar / ToolBar / Toolbox\n工具与操作入口'),(465,165,360,85,'Canvas\n画布编辑与文档状态'),(915,165,355,85,'PropertyPanel\n属性显示与修改'),(200,340,370,90,'Component / Pin / Circuit\n元件逻辑与拓扑求值'),(730,340,370,90,'Wire / CircuitFile\n线路几何与文件读写')],[(0,1),(0,2),(0,3),(2,4),(2,5)],(1300,470))
p('图3-1 系统模块结构图。连线表示主要控制或依赖方向，属性更新通过主窗口绑定的回调与画布协作完成。')
h('3.2.1 系统用例模块映射表',3)
table(1,[
['UC01','OnNew','新建电路','清空画布和历史','MainFrame菜单','ConfirmDiscardChanges；ClearDocument','先询问保存'],
['UC02','OnOpen','打开电路','读取校验并重建电路','MainFrame菜单','CircuitFile::Load；ImportDocument','失败保留画布'],
['UC03','SaveDocument','保存电路','导出并写入XML','MainFrame菜单','ExportDocument；CircuitFile::Save','支持另存为'],
['UC04','OnLeftDown','放置与选择','按工具创建或选取元件','Canvas鼠标事件','元件构造；命中判定','Canvas.cpp'],
['UC05','HandleWireClick','连接引脚','选择两端并校验连接','Canvas鼠标事件','Circuit::Connect；RecomputeCircuit','CanvasWiring.cpp'],
['UC06','SetGateInputCount','修改输入数','调整门引脚并清理连线','PropertyPanel回调','SetInputCount；PruneInvalidConnections','2～16；NOT固定'],
['UC07','RotateSelection','旋转元件','改变角度并更新线路','菜单或键盘事件','几何变换；MarkModified','以90度为步长'],
['UC08','ToggleSelectedInput','切换输入','反转选中输入的0/1','菜单或画布操作','RecomputeCircuit','只对输入端口有效'],
['UC09','EvaluateCircuit','计算电路','拓扑求值并刷新显示','仿真菜单F5','Circuit::Evaluate','无延迟组合逻辑'],
['UC10','HandleEditCommand','剪贴板与删除','复制、剪切、粘贴或删除','MainFrame编辑菜单','Canvas编辑辅助函数','复制元件不含外部线'],
['UC11','Undo','撤销编辑','恢复最近文档快照','编辑菜单或Ctrl+Z','ReplaceDocument','最多50步'],
['UC12','EditTextBox','编辑文字','修改注释文字内容','Canvas双击事件','wxTextCtrl；FinishTextEditing','支持中文及多行'],
['UC13','UpdateWireDrag','调整线路','拖动线段改变直角路径','Canvas鼠标移动','SetConnectionRoute','Esc或右键取消']
],'用例英文名沿用主要实现函数；“调用对象”表示发起者，“被调用对象”表示该操作依赖的处理。不同操作共享Canvas及模型服务。')
h('3.2.2 系统用例结构图',3)
diagram('current_usecases.png',[(15,165,210,100,'使用者'),(355,20,370,85,'文件管理\n新建、打开、保存'),(355,165,370,100,'画布编辑\n放置、旋转、文字、撤销'),(355,345,370,90,'电路构建\n连线、调整路径与引脚'),(885,165,370,100,'逻辑验证\n切换输入、重新计算')],[(0,1),(0,2),(0,3),(2,4),(3,4)],(1300,470))
p('图3-2 系统用例结构图。文件操作与编辑操作形成电路文档，连接或输入改变后触发计算并更新显示。')
h('3.2.3 设计类结构',3)
p('Component是抽象基类，包含name、inputs、outputs及纯虚函数Evaluate。MultiInputGate继承Component并统一管理2～16个输入；AndGate、OrGate、NandGate、NorGate、XorGate继承MultiInputGate。NotGate、InputComponent和OutputComponent直接继承Component。Pin记录引脚名称、方向和LogicValue。')
p('Circuit管理WireConnection集合，每条连接由两个PinReference描述源输出与目标输入，并可保存手动路径。PinReference以Component指针、方向和索引引用现有引脚，不拥有元件。Canvas保存PlacedComponent、文字框、Circuit及撤销快照，负责元件生命周期；删除元件时必须先清理关联连接。')
diagram('current_classes.png',[(420,15,450,75,'Component\nname / inputs / outputs / Evaluate'),(40,160,390,80,'MultiInputGate\nSetInputCount / 2～16'),(750,160,510,80,'NotGate / InputComponent\nOutputComponent'),(40,315,480,80,'AndGate / OrGate / NandGate\nNorGate / XorGate'),(750,315,510,80,'Circuit → WireConnection\nPinReference → Component')],[(0,1),(0,2),(1,3)],(1300,430))
p('图3-3 核心类关系图。向下连线表示派生关系；右下框说明连接对元件的引用关系，Canvas负责协调这些对象。')
h('3.2.4 关键处理过程或者算法的设计',3)
p('（1）连接合法性检查。Canvas把鼠标点击转换为PinReference，Circuit::Connect先清理无效线路，再校验引脚存在性与方向，并将两端统一为输出到输入；随后检查重复连接和目标输入占用。WouldCreateCycle从目标元件沿现有出边搜索，若能够到达源元件则拒绝新连接。搜索采用显式待访问栈和访问集合，避免递归调用。')
p('（2）逻辑求值。Circuit::Evaluate建立元件索引、入度和出边集合，先将全部输入引脚置为Low，再把入度为零的元件放入队列。每次取出一个元件调用Evaluate，把输出沿导线写入后继输入，并减少后继入度；入度归零的后继进入队列。实际求值数等于节点数时成功，否则返回false。建立邻接关系后，一次求值的主要时间和空间开销均为O(V+E)。连接时的环路搜索会反复扫描连接集合，不应将其实现宣称为同样的线性复杂度。')
p('（3）门逻辑规则。AND在全部输入为1时输出1；OR在至少一个输入为1时输出1；NOT反相；NAND和NOR分别对AND和OR结果取反；XOR在高电平输入数为奇数时输出1。InputComponent保存用户设置的电平，OutputComponent显示其输入。切换输入、增删线路或修改参数后重新计算，求值结果通过线路颜色和端口数字反馈。')
p('（4）几何与路径。画布使用统一几何方法计算元件边界、引脚位置、旋转坐标和命中区域。Wire负责自动直角路径、绘制与点击判定；拖动线路时固定端点并调整线段，提交前校验坐标与路径点数量。交叉线路不自动形成电气连接，同一输出的多条连接形成分支。')
p('（5）文档提交。打开文件时，MainFrame先处理未保存修改，再由CircuitFile::Load解析并验证临时CircuitDocument，Canvas::ImportDocument构建新模型，成功后替换当前状态并清空历史。保存时，Canvas导出元件、连接和文字，CircuitFile::Save校验后写入同目录临时文件，完成后替换目标，主窗口再更新路径与保存标志。')
p('（6）撤销。每次有效操作记录修改前的CircuitDocument，连续重复快照不占用额外步数，超过50步删除最旧记录；拖动过程合并为一步。Undo通过ReplaceDocument恢复模型，失败时保留画布和历史。保存不清空撤销历史，新建和打开会清空。文字编辑期间先由文字控件处理当前编辑撤销，结束后文字变更进入画布撤销范围。')
h('3.3 系统接口设计',2)
h('3.3.1 系统接口表',3)
el=deepcopy(tables[2]); body.insert(len(body)-1,el)
h('3.3.2 系统接口传输协议说明',3)
para()
h('3.4 系统完整性设计',2)
para()
h('4 系统出错处理设计')
p('错误处理分为操作拒绝、文件失败和状态恢复三个层次。连线错误通过状态栏提示并阻止错误模型进入电路；文件错误返回说明且不覆盖当前画布；编辑恢复依赖撤销快照和未保存询问。错误编号为报告内部标识，代码以ConnectionResult、bool和wxString错误说明表达。')
h('4.1 系统出错处理表',2)
table(3,[
['E01','重复连接','同一对引脚再次连线','这两个引脚已连接','取消本次连线','Duplicate'],
['E02','方向相同','连接两个输入或两个输出','只能连接输出与输入引脚','重新选择互补端点','SameDirection'],
['E03','输入多驱动','输入已连接其他导线','该输入已有导线','删除旧线后重连','InputOccupied'],
['E04','组合反馈环路','新增连接使拓扑成环','不支持组合逻辑反馈环路','拒绝连接并修改电路','Cycle'],
['E05','无效引脚','对象不存在或索引越界','返回InvalidPin状态','取消未完成连接并重新选择','不固定界面文本'],
['E06','非法门参数','输入数超出2～16','SetInputCount返回false','保持原参数','构造时可抛异常'],
['E07','文档格式错误','XML损坏、版本或字段不合法','Load返回false和error说明','保留画布，修复文件后重试','不得部分导入'],
['E08','文档资源超限','文件、对象、文字或路径超限','Validate或Load返回错误说明','缩减文档并重新读取','见2.1边界'],
['E09','保存失败','目录不可写或写入、替换失败','Save返回false和error说明','检查路径权限并重试或另存','不标记为已保存'],
['E10','未保存离开','新建、打开或退出时文档已改','保存确认对话框','选择保存、放弃或取消','属于操作保护']
],'E01～E04为源代码中的状态栏提示原文；其他错误信息按返回机制描述，具体文本由校验失败分支产生。')
h('4.2 维护处理过程表',2)
table(4,[
['E01～E05','Circuit::Connect','连接检查','拒绝非法连接并返回原因','两个PinReference','ConnectionResult','不插入错误连接'],
['E01、E05','CancelPendingWire','取消连接','清理未完成连线与预览','当前画布交互状态','无返回值','由界面按分支调用'],
['E06','SetInputCount','调整引脚数量','检查范围并保留合法引脚','输入数量count','bool','非法时保持原状'],
['E07、E08','CircuitFile::Load','文件读取恢复','解析临时文档并完整校验','path、document、error','bool及error','失败不改变document'],
['E07、E08','CircuitFile::Validate','文档合法性校验','核对对象、参数及连接','document、error','bool及error','也用于保存前'],
['E07、E08','ReplaceDocument','画布模型替换','完整构建后提交新模型','document、error、resetHistory','bool及error','失败保留当前状态'],
['E09','CircuitFile::Save','安全保存','先写临时文件再替换目标','path、document、error','bool及error','成功后才MarkSaved'],
['E10','ConfirmDiscardChanges','离开前确认','询问保存并决定继续操作','当前修改标志与文件路径','bool','取消或保存失败阻止离开']
],'函数名称与源码对应；入口参数按概要层次列出，引用参数中的error承担错误说明输出。')
h('5 技术设计')
p('技术选择服务于本地电路编辑、组合逻辑求值与可恢复的文件存储。界面与文件层使用wxWidgets提供的控件、绘图、事件和XML能力，核心电路运算由C++类及标准容器完成。')
h('5.1 系统开发技术说明表',2)
p('开发语言与工具：C++20，Visual Studio解决方案及vcxproj工程，MSVC平台工具集v145。项目提供Win32和x64等配置，实际依赖路径必须与目标配置一致，交付前以选定配置进行构建验证。')
p('界面与图形：wxWidgets 3.3.3，使用wxFrame、wxPanel、wxSplitterWindow、树控件、属性控件、菜单工具栏、wxDC及缓冲绘图。鼠标和键盘事件驱动工具切换、命中检测和编辑状态变化，wxTimer支持拖动与文字交互。')
p('模型与算法：抽象基类与继承表达逻辑元件；vector保存对象序列，unordered_map、unordered_set及queue用于拓扑建图、访问记录和队列求值。线路几何与逻辑连接分工处理。')
p('数据与验证：wxXmlDocument处理UTF-8 XML，CircuitDocument作为画布与文件之间的值数据载体，PowerShell脚本与C++回归程序验证门逻辑、图形交互及存储边界。程序不依赖数据库服务和网络服务器。')
h('5.2 开发技术应用说明',2)
p('界面采用事件绑定和回调协调：主窗口把菜单与快捷键映射到Canvas操作，工具栏与元件库保持工具选择一致；Canvas把选中元件、线路和文字状态回传属性面板。编辑状态在Canvas内部统一维护，避免不同入口产生不一致行为。')
p('多态Evaluate使Circuit能够统一处理不同逻辑门，新增门类可复用既有拓扑传播。可变输入门复用MultiInputGate管理边界与引脚，NOT和端口保持自身规则。模型不依赖元件的屏幕位置，因此旋转和移动不会改变逻辑关系。')
p('CircuitDocument使用值数据承载元件、线路和文字，使保存、导入、快照撤销复用同一结构。运行态连接引用对象，文件态连接引用ID，导入时通过映射还原。文件校验复用Circuit的连接规则，减少交互建立与文件导入对合法电路定义的偏差。')
p('维护验证使用tests/run_regression.ps1与tests/run_storage_regression.ps1。前者覆盖门真值、引脚参数、连线及画布操作，后者覆盖XML往返、非法文件及边界保护。调试符号工具异常时可按项目说明使用-WithoutDebugSymbols；测试通过情况应以实际执行记录为准。')
h('6 数据库设计')
p('本系统未使用关系型或其他数据库管理系统，持久化对象是本地XML电路文件。本节定义概要层次的数据结构和一致性规则，为后续文件格式维护提供依据。')
p('电路文档CircuitDocument由components、wires、texts三个集合组成。根元素为circuit，version属性当前为1。逻辑输出属于可重算结果，载入后由电路求值得到，避免把过期的显示值视为权威状态。')
p('元件记录CircuitComponentData包含正整数id、type、x、y、inputCount、high和rotation。id在一份文档内唯一；type限定为当前支持的门与端口；high保存输入端口设置；rotation保存以90度为单位的方向。坐标不作为连接键。')
p('连线记录CircuitWireData包含sourceId、sourcePin、targetId、targetPin及route。元件ID必须存在，引脚序号从0开始，源端是输出、目标端是输入。route为空时自动布线，非空时记录源端、折点和目标端，路径必须满足直角段与资源限制。不得存在重复连接、同输入多驱动或组合逻辑环。')
p('文字记录CircuitTextData包含矩形rect、pointSize和text，保存位置、宽高、字号及文字内容。使用UTF-8支持中文与多行；尺寸、字号及字符数必须通过校验。XML版本1的旧文件缺少rotation或route时，分别按0度及自动布线恢复。')
p('读取使用临时文档并在完整验证后替换，保存先写同目录临时文件再替换目标。元件删除必须清除关联线路，输入数减少必须删除越界连接；复制单个元件不复制外部导线。当前未提供账户权限、事务日志、数据库索引或多用户并发编辑。')
h('7 词汇表')
for x in ['数字电路：用有限的逻辑状态表达和处理信息的电路，本系统仅使用0与1。','组合逻辑：输出由当前输入决定的逻辑网络，本系统不包含记忆状态和时钟行为。','逻辑门：执行与、或、非、与非、或非、异或等运算的基本元件。','引脚（Pin）：元件传入或传出逻辑值的连接点，按输入与输出区分。','电平（LogicValue）：本系统中代表逻辑0的Low或代表逻辑1的High，不表示实际电压。','扇出与多驱动：同一输出连接多个输入称为扇出；同一输入接收多个输出称为多驱动，后者在本系统中被禁止。','反馈环路：沿有向线路能够回到原元件的连接，当前组合逻辑模型不接受此类结构。','拓扑排序：按依赖先后安排元件计算，使后继在其输入已经准备好后再求值。','自动直角布线：由水平段与垂直段构成的自动线路路径；视觉交叉不代表电气连接。','网格吸附：把放置或移动的位置对齐到画布网格，便于排列元件。','文档快照：某一时刻全部元件、线路和文字的值数据副本，用于撤销恢复。','XML（Extensible Markup Language）：以标签组织数据的文本格式，本系统用其保存电路。','EDA（Electronic Design Automation）：电子设计自动化，本项目实现其中的基础数字电路编辑与组合逻辑验证。']: p(x)
h('8 进度计划')
p('当前代码已包含窗口与工具面板、六类门及端口、引脚连接、直角线路、组合逻辑传播、文字注释、旋转、撤销和XML存取。以下为以本报告评审通过日T为起点的建议收尾计划，属于后续安排，不作为历史完成日期或测试通过记录。')
p('T至T+1工作日：项目负责人和需求确认人员核对功能范围、风险及验收样例，完成文档评审。界面与模型开发人员共同检查报告中的用例映射和函数名称。')
p('T+2至T+3工作日：界面开发人员检查放置、选择、旋转、网格、文字、剪贴板和撤销的交互一致性；模型开发人员检查变更输入数、删除元件和拖动线路后的连接与求值。测试人员准备门真值表、多级电路、扇出及非法环路场景。')
p('T+4至T+5工作日：存储开发人员检查中文路径、多行文字、旋转和手动路径的文件往返以及非法文件恢复；测试人员执行现有两类回归脚本，记录失败项，开发人员修复后复验相关范围。')
p('T+6工作日：项目负责人组织交付检查，确认选定构建配置、运行资源目录、样例文件和使用说明。文档人员同步最终代码边界，归档版本与测试记录。')
p('人员按职责安排为项目协调、界面开发、逻辑与存储开发、测试及文档维护；小型团队可由同一成员兼任，实际人数和姓名由负责人分配。时钟、存储器、复用器等扩展另行评估模型和工期，不计入上述收尾计划。')
d.core_properties.title='数字电路编辑与逻辑仿真系统概要设计报告'
d.core_properties.author=''
d.core_properties.subject='概要设计'
OUT.parent.mkdir(exist_ok=True)
temp=TMP/'authored.docx'; d.save(temp)
# Keep untouched template package parts byte-for-byte.
with zipfile.ZipFile(REF) as src, zipfile.ZipFile(temp) as changed, zipfile.ZipFile(OUT,'w',zipfile.ZIP_DEFLATED) as dst:
    for item in src.infolist():
        if item.filename=='word/document.xml': dst.writestr(item,changed.read(item.filename))
        else: dst.writestr(item,src.read(item.filename))
    for item in changed.infolist():
        if item.filename not in src.namelist(): dst.writestr(item,changed.read(item.filename))
    # New pictures require relationships and content type registration.
    # Those parts are replaced in a second pass below.
with zipfile.ZipFile(OUT) as z: payload={n:z.read(n) for n in z.namelist()}
with zipfile.ZipFile(temp) as z:
    for n in ['word/_rels/document.xml.rels','[Content_Types].xml','docProps/core.xml']: payload[n]=z.read(n)
with zipfile.ZipFile(OUT,'w',zipfile.ZIP_DEFLATED) as z:
    for n,b in payload.items(): z.writestr(n,b)
check=Document(OUT)
assert len(check.tables)==5
assert len(check.tables[0].rows)==2
assert not any('成绩管理' in p.text for p in check.paragraphs)
print(OUT)
print('paragraphs',len(check.paragraphs),'tables',len(check.tables))
