# lists the connected parts of one material's faces in a glTF: place, size, normal, uv range
import json, sys, os
import numpy as np
gltf, mat = sys.argv[1], sys.argv[2]
g = json.load(open(gltf))
buf = open(os.path.join(os.path.dirname(gltf), g['buffers'][0]['uri']), 'rb').read()
CT = {5121: 'B', 5123: 'H', 5125: 'I', 5126: 'f'}; NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
def acc(i):
    a = g['accessors'][i]; v = g['bufferViews'][a['bufferView']]; n = NC[a['type']]; dt = np.dtype('<' + CT[a['componentType']])
    off = v.get('byteOffset', 0) + a.get('byteOffset', 0); st = v.get('byteStride', 0)
    if st and st != dt.itemsize * n:
        raw = np.frombuffer(buf, np.uint8, st * a['count'], off).reshape(a['count'], st)[:, :dt.itemsize * n]
        return np.ascontiguousarray(raw).view(dt).reshape(a['count'], n)
    return np.frombuffer(buf, dt, a['count'] * n, off).reshape(a['count'], n)
for prim in g['meshes'][0]['primitives']:
    if g['materials'][prim['material']]['name'] != mat: continue
    P = acc(prim['attributes']['POSITION']).astype(float).copy(); P[:, 2] *= -1
    N = acc(prim['attributes']['NORMAL']).astype(float).copy(); N[:, 2] *= -1
    UV = acc(prim['attributes']['TEXCOORD_0']).astype(float)
    I = acc(prim['indices']).reshape(-1, 3)
    # weld by position
    key = {}; w = np.zeros(len(P), int)
    for i, p in enumerate(P):
        w[i] = key.setdefault(tuple(np.round(p, 4)), len(key))
    par = list(range(len(key)))
    def find(x):
        while par[x] != x: par[x] = par[par[x]]; x = par[x]
        return x
    for t in I:
        a, b, c = (find(w[v]) for v in t); par[b] = a; par[c] = a
    comps = {}
    for ti, t in enumerate(I): comps.setdefault(find(w[t[0]]), []).append(ti)
    for k, (r, ts) in enumerate(sorted(comps.items(), key=lambda kv: P[I[kv[1]].reshape(-1)][:, 0].mean())):
        vi = np.unique(I[ts].reshape(-1)); p = P[vi]; uv = UV[vi]; n = N[vi].mean(0); n /= np.linalg.norm(n)
        area = sum(np.linalg.norm(np.cross(P[t[1]] - P[t[0]], P[t[2]] - P[t[0]])) / 2 for t in I[ts])
        print('%2d: %2d tris %2d verts area %.4f centre %6.3f %6.3f %6.3f size %.3f %.3f %.3f normal %5.2f %5.2f %5.2f uv %.3f..%.3f %.3f..%.3f' % (
            k, len(ts), len(vi), area, *p.mean(0), *(p.max(0) - p.min(0)), *n, uv[:, 0].min(), uv[:, 0].max(), uv[:, 1].min(), uv[:, 1].max()))
