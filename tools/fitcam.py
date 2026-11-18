# fitcam.py — least-squares pivot fit over "v <eye xyz> <forward xyz>" lines (Max Payne recomp view-matrix trace).
# Feed it mpcam.log; radius spread tells you whether the samples are a pure orbit (small) or mixed cameras (huge).
import sys, math

pts = []
for line in open(sys.argv[1] if len(sys.argv) > 1 else "mpcam.log"):
    p = line.split()
    if len(p) != 7 or p[0] != "v": continue
    e = [float(x) for x in p[1:4]]; f = [float(x) for x in p[4:7]]
    n = math.sqrt(sum(c * c for c in f))
    if n < 1e-6: continue
    f = [c / n for c in f]
    if e == [0, 0, 0]: continue
    pts.append((e, f))
print("samples", len(pts))
if len(pts) < 10: sys.exit("not enough")

# least squares pivot: minimise sum |(p-e) - ((p-e).f) f|^2  ->  sum (I-ff^T) p = sum (I-ff^T) e
A = [[0.0] * 3 for _ in range(3)]; b = [0.0] * 3
for e, f in pts:
    for i in range(3):
        for j in range(3):
            A[i][j] += (1.0 if i == j else 0.0) - f[i] * f[j]
        b[i] += e[i] - sum(f[k] * e[k] * f[i] for k in range(3))
# solve 3x3
def solve(A, b):
    M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for c in range(3):
        piv = max(range(c, 3), key=lambda r: abs(M[r][c]))
        if abs(M[piv][c]) < 1e-9: return None
        M[c], M[piv] = M[piv], M[c]
        for r in range(3):
            if r != c:
                k = M[r][c] / M[c][c]
                for cc in range(c, 4): M[r][cc] -= k * M[c][cc]
    return [M[i][3] / M[i][i] for i in range(3)]

piv = solve(A, b)
print("pivot %.3f %.3f %.3f" % tuple(piv))
rs = []
for e, f in pts:
    rs.append(math.sqrt(sum((piv[i] - e[i]) ** 2 for i in range(3))))
rs.sort()
print("radius min/median/max %.3f %.3f %.3f" % (rs[0], rs[len(rs) // 2], rs[-1]))
# pitch vs eye height above pivot
for e, f in pts[:: max(1, len(pts) // 25)]:
    pitch = math.degrees(math.asin(max(-1, min(1, f[2]))))
    print("pitch %6.1f  eye_z-piv %+6.3f  r %.3f" % (pitch, e[2] - piv[2], math.sqrt(sum((piv[i] - e[i]) ** 2 for i in range(3)))))
