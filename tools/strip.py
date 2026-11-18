import sys, glob
from PIL import Image
# usage: strip.py out.jpg TAG1 TAG2 ...  (rows = tags, columns = frames) from build/strips
d = "/media/montel/Extreme SSD/Games/Grand Theft Auto San Andreas/mod projects/max-payne-combat/build/strips/"
rows = [[Image.open(f).convert("RGB") for f in sorted(glob.glob(d + t + "_*.png"))] for t in sys.argv[2:]]
w, h = rows[0][0].size; s = 0.5
out = Image.new("RGB", (int(w * s) * max(map(len, rows)), int(h * s) * len(rows)), "white")
for r, row in enumerate(rows):
    for c, im in enumerate(row):
        out.paste(im.resize((int(w * s), int(h * s))), (c * int(w * s), r * int(h * s)))
out.save(sys.argv[1], quality=85)
