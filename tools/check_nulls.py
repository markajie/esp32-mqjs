
import re
h=open('components/mjs_engine/generated/mqjs_stdlib.h',encoding='utf-8').read()
# map offsets
m=re.search(r'/\* atom_table \*/(.*?)/\* sorted atom table',h,re.S)
off={}
for line in m.group(1).splitlines():
    mm=re.search(r'/\* (.*) \(offset=(\d+)\) \*/',line)
    if mm: off[int(mm.group(2))]=mm.group(1)
# parse cfunc entries
c=re.search(r'static const JSCFunctionDef js_c_function_table\[\] = \{(.*?)\n\};',h,re.S).group(1)
entries=re.findall(r'\{ \{ \.(\w+) = ([\w"]+) \},\s*JS_ROM_VALUE\((\d+)\),\s*JS_CFUNC_\w+, (\d+), ([^}]+) \},',c)
for e in entries:
    if e[1]=='NULL':
        print('NULL',e,'atom=',off.get(int(e[2])))
