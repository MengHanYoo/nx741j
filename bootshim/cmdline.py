import re, sys
text = open(sys.argv[1]).read()
m = re.search(r'kernel_args\s*=\s*\[(.*?)\]', text, re.S)
if not m:
    print('keepsyms=1')
    sys.exit(0)
args = re.findall(r'"([^"]*)"', m.group(1))
print(' '.join(a for a in args if a))
