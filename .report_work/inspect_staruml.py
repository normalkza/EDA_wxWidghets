import json
import struct
from collections import Counter
from pathlib import Path

ASAR = Path('C:/Program Files/StarUML/resources/app.asar')
with ASAR.open('rb') as f:
    f.seek(4)
    header_size = struct.unpack('<I', f.read(4))[0]
    f.seek(12)
    json_size = struct.unpack('<I', f.read(4))[0]
    header = json.loads(f.read(json_size))

def read(path):
    node = header
    for part in path.strip('/').split('/'):
        node = node['files'][part]
    with ASAR.open('rb') as f:
        f.seek(8 + header_size + int(node['offset']))
        return f.read(int(node['size'])).decode('utf-8')

def walk(obj):
    if isinstance(obj, dict):
        yield obj
        for val in obj.values():
            yield from walk(val)
    elif isinstance(obj, list):
        for val in obj:
            yield from walk(val)

if __name__ == '__main__':
    import sys
    if len(sys.argv)>1 and sys.argv[1]=='--list':
        def paths(node, prefix=''):
            for name, child in node.get('files', {}).items():
                path = prefix + '/' + name
                if 'files' in child:
                    yield from paths(child, path)
                elif path.endswith('.mdj'):
                    yield path
        print('\n'.join(paths(header)))
        sys.exit(0)
    path = sys.argv[1] if len(sys.argv)>1 else '/extensions/essential/uml/templates/UMLConventional.mdj'
    data = json.loads(read(path))
    counts = Counter(x['_type'] for x in walk(data) if '_type' in x)
    print('root',data.get('_type'),data.get('name'))
    print('types',counts.most_common(80))
    for d in walk(data):
        if d.get('_type', '').endswith('Diagram'):
            print('DIAGRAM', d.get('_type'),d.get('name'),'views',len(d.get('ownedViews',[])))
