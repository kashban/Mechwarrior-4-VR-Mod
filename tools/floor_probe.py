# What does the eye see looking down? Reads a .mwc and prints a coarse top view of the faces below the
# origin (the eye): python floor_probe.py <cockpit.mwc>
# '#' = an upward face there (floor), ':' = only downward / steep faces, '.' = nothing (open)
import struct, sys
import numpy as np

f = open(sys.argv[1], 'rb')
assert f.read(4) == b'MWC1'
n = struct.unpack('<I', f.read(4))[0]
X0, X1, Z0, Z1, S = -1.5, 1.5, -1.5, 2.0, 0.06
W, H = int((X1 - X0) / S), int((Z1 - Z0) / S)
up = np.full((H, W), 9.0)
any_ = np.zeros((H, W), bool)
for _ in range(n):
    name, nv, ni, cs, es, k = struct.unpack('<48sIIIIf', f.read(68))
    V = np.frombuffer(f.read(nv * 32), '<f4').reshape(nv, 8)
    I = np.frombuffer(f.read(ni * 4), '<u4').reshape(-1, 3)
    for s, c in ((cs, 1), (es, 1)):
        while s:
            f.seek(s * s * 4, 1)
            s //= 2
    P = V[:, :3]
    T = P[I]
    nrm = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    ln = np.linalg.norm(nrm, axis=1) + 1e-12
    vn = V[I[:, 0], 3:6]
    flip = np.sign((nrm * vn).sum(1))
    ny = nrm[:, 1] / ln * flip
    print('%-28s y %.2f..%.2f  x %.2f..%.2f  z %.2f..%.2f  tris %d' % (name.split(b'\0')[0].decode(), P[:, 1].min(), P[:, 1].max(), P[:, 0].min(), P[:, 0].max(), P[:, 2].min(), P[:, 2].max(), len(I)))
    for t, y in zip(T, ny):
        if t[:, 1].max() > -0.2:
            continue
        # sample points in the triangle
        for a, b in ((1 / 3, 1 / 3), (0.6, 0.2), (0.2, 0.6), (0.2, 0.2), (0.05, 0.05), (0.9, 0.05), (0.05, 0.9)):
            p = t[0] + a * (t[1] - t[0]) + b * (t[2] - t[0])
            i, j = int((p[0] - X0) / S), int((p[2] - Z0) / S)
            if 0 <= i < W and 0 <= j < H:
                any_[j, i] = True
                if y > 0.5:
                    up[j, i] = min(up[j, i], -p[1])
print('top view, forward = up, %.0f cm cells; digits: depth of the floor below the eye in 20 cm steps' % (S * 100))
for j in range(H - 1, -1, -1):
    print(''.join((str(min(9, int(up[j, i] / 0.2))) if up[j, i] < 9 else (':' if any_[j, i] else '.')) for i in range(W)))
