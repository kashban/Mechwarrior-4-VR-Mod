# install_trees.py <build_trees output> <game folder>: models into Content\Models\clutter\mw4vr, the tree part of
# Content\Clutter\clutter.txt (from the line "; MW4VR trees" on) replaced; the old clutter.txt kept beside it
import os, shutil, sys
src, game = sys.argv[1], sys.argv[2]
dst = os.path.join(game, 'Content', 'Models', 'clutter', 'mw4vr')
os.makedirs(dst, exist_ok=True)
n = 0
for f in os.listdir(src):
    if f.endswith(('.obj', '.material', '_cards.png', '_bark.png', '.mtl')):
        shutil.copy2(os.path.join(src, f), os.path.join(dst, f))
        n += 1
p = os.path.join(game, 'Content', 'Clutter', 'clutter.txt')
old = open(p, encoding='latin-1').read()
k = 1
while os.path.exists('%s.before_trees%d' % (p, k)):
    k += 1
shutil.copy2(p, '%s.before_trees%d' % (p, k))
i = old.find('; MW4VR trees')
head = old if i < 0 else old[:i]
new = open(os.path.join(src, 'clutter_trees.txt'), encoding='latin-1').read()
open(p, 'w', encoding='latin-1', newline='').write(head.rstrip('\r\n') + '\r\n\r\n' + new.replace('\r\n', '\n').replace('\n', '\r\n') + '\r\n')
kinds = [l for l in (head + new).splitlines() if l.startswith('[')]
print('%d files copied; clutter.txt: %d kinds (backup .before_trees%d)' % (n, len(kinds), k))
