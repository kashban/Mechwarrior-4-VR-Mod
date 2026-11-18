# Fetches Poly Haven models (CC0, https://polyhaven.com) as 1k glTF: the .gltf, its .bin and the colour pictures -
# what build_trees.py draws its trees from.
#   python fetch_polyhaven.py <folder> <asset> [<asset> ...]      -> <folder>/<asset>/...
import hashlib
import json
import os
import sys
import urllib.request

AGENT = {'User-Agent': 'tree-card-builder'}


class HttpsOnly(urllib.request.HTTPRedirectHandler):
    """A redirect is followed to another https address alone."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not newurl.startswith('https://'):
            raise SystemExit('refused redirect to ' + newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


OPENER = urllib.request.build_opener(HttpsOnly)


def get(url):
    if not url.startswith('https://'):  # (urllib would also open file: and the like)
        raise SystemExit('refused address ' + url)
    return OPENER.open(urllib.request.Request(url, headers=AGENT))  # noqa: S310 - https alone: above, and HttpsOnly


def main(folder, names):
    for name in names:
        g = json.load(get('https://api.polyhaven.com/files/' + name))['gltf']['1k']['gltf']
        out = os.path.join(folder, name)
        todo = [(name + '_1k.gltf', g['url'], g['size'], g.get('md5'))]
        for rel, v in g.get('include', {}).items():
            base = rel.replace('\\', '/').split('/')[-1]
            if base.endswith('.bin') or '_diff_' in base or '_alpha_' in base:
                todo.append((rel.replace('\\', '/'), v['url'], v['size'], v.get('md5')))
        for rel, url, size, md5 in todo:
            if '..' in rel or rel.startswith('/') or ':' in rel:
                raise SystemExit('refused path ' + rel)
            path = os.path.join(out, *rel.split('/'))
            if os.path.exists(path) and os.path.getsize(path) == size:
                continue
            os.makedirs(os.path.dirname(path), exist_ok=True)
            seen = hashlib.md5(
                usedforsecurity=False
            )  # (the site's own checksum of the file: against a broken download)
            with get(url) as r, open(path + '.part', 'wb') as f:
                while True:
                    b = r.read(1 << 20)
                    if not b:
                        break
                    seen.update(b)
                    f.write(b)
            if os.path.getsize(path + '.part') != size:
                raise SystemExit('{}: size differs'.format(rel))
            if md5 and seen.hexdigest() != md5.lower():
                raise SystemExit('{}: checksum differs'.format(rel))
            os.replace(path + '.part', path)
        print('%s: %d files, %.1f MB' % (name, len(todo), sum(t[2] for t in todo) / 1e6))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2:])
