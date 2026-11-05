from docx import Document
from pathlib import Path

p = Path('报告文档') / '概要设计报告_3.3_3.4.docx'
d = Document(p)
matches = [x for x in d.paragraphs if x.text.strip() == '3.4 系统完整性设计']
if len(matches) > 1:
    matches[-1]._element.getparent().remove(matches[-1]._element)
d.save(p)
print('remaining', sum(x.text.strip() == '3.4 系统完整性设计' for x in d.paragraphs))
