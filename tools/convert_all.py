# Converts every cockpit exported by tools/export_cockpits.ps1 into a .mwc (and its .ini) for the mod.
#   python tools/convert_all.py <export root> <out folder> [max texture size] [mech ...]
# The list is <export root>/cockpits.txt (mech <tab> path of the glTF without its ending [<tab> the export root of
# that cockpit, for a mod's]). A cockpit whose
# .mwc is already there is left alone; delete the file to have it made again.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cockpit_convert as cc


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__ or 'python tools/convert_all.py <export root> <out folder> [max texture size] [mech ...]')
    root, out = argv[1], argv[2]
    rest = argv[3:]
    tmax = int(rest.pop(0)) if rest and rest[0].isdigit() else cc.TMAX
    only = {m.lower() for m in rest}
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(root, 'cockpits.txt'), encoding='utf-8-sig') as f:
        rows = [line.rstrip('\n').split('\t') for line in f if '\t' in line]
    done = failed = 0
    for mech, path, *own in rows:
        name = mech.lower()
        # (the name becomes a file's name in the out folder: a list made by hand could leave it)
        if not name or name in ('.', '..') or any(c in name for c in '\\/:*?"<>|') or '..' in name:
            failed += 1
            print('%-16s FAILED: not a file name' % name)
            continue
        target = os.path.join(out, name + '.mwc')
        if (only and name not in only) or os.path.exists(target):
            continue
        said = []
        try:
            mod = bool(own and own[0])  # a mod's cockpit: its own export root, and shaded by its vertex colours
            cc.convert(path + '.gltf', own[0] if mod else root, target, tmax, log=lambda *a: said.append(' '.join(str(x) for x in a)), shade=mod)
        except Exception as e:  # one cockpit that cannot be read does not stop the others
            failed += 1
            if os.path.exists(target):
                os.remove(target)
            print('%-16s FAILED: %s' % (name, e))
            continue
        done += 1
        screens = sum(1 for s in said if s.startswith('  screen '))
        print('%-16s %2d monitors, %4d MB' % (name, screens, os.path.getsize(target) >> 20))
    print('%d converted, %d failed' % (done, failed))
    detail = os.path.join(out, cc.DETAIL_FILE)
    if not os.path.exists(detail):  # what painted metal is drawn with: from the first export that has the game's pictures
        for r in [own[0] for _, _, *own in rows if own and own[0]] + [root]:
            im = cc.detail_map(r)
            if im:
                cc.write_detail(detail, im)
                print('painted metal: %s written' % cc.DETAIL_FILE)
                break


if __name__ == '__main__':
    main(sys.argv)
