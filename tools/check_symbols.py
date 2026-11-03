
import re
h=open('components/mjs_engine/generated/mqjs_stdlib.h',encoding='utf-8').read()
m=re.search(r'static const JSCFunctionDef js_c_function_table\[\] = \{(.*?)\n\};',h,re.S)
names=[]
for mm in re.finditer(r'\.\w+\s*=\s*(\w+) \}',m.group(1)):
    names.append(mm.group(1))
src=open('mquickjs/mquickjs.c',encoding='utf-8').read()
missing=[]
for n in sorted(set(names)):
    if not re.search(r'\b'+re.escape(n)+r'\s*\(',src):
        missing.append(n)
print('cfuncs',len(names),'unique',len(set(names)))
print('missing in mquickjs.c:')
for n in missing: print(' ',n)
