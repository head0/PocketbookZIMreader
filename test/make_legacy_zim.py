#!/usr/bin/env python3
"""Hand-writes an old-style ZIM (v5, namespaces A/I, xz cluster + uncompressed cluster)."""
import lzma, struct, sys

def dirent(mime, ns, cluster, blob, url, title=""):
    return struct.pack("<HBcIII", mime, 0, ns.encode(), 0, cluster, blob) + url.encode() + b"\0" + title.encode() + b"\0"
def redirect(ns, target, url, title=""):
    return struct.pack("<HBcII", 0xFFFF, 0, ns.encode(), 0, target) + url.encode() + b"\0" + title.encode() + b"\0"
def cluster(blobs, comp):
    offs, pos = [], 4 * (len(blobs) + 1)
    for b in blobs: offs.append(pos); pos += len(b)
    offs.append(pos)
    raw = b"".join(struct.pack("<I", o) for o in offs) + b"".join(blobs)
    return bytes([comp]) + (lzma.compress(raw, format=lzma.FORMAT_XZ) if comp == 4 else raw)

png = open(sys.argv[2], "rb").read()
html = ["<html><head><title>Accueil</title></head><body><h1>Ancien format</h1><p>Vers <a href='Page_deux'>la page deux</a> "
        "et <a href='Vieux'>une redirection</a>.</p><img src='../I/logo.png'></body></html>",
        "<html><body><h1>Page deux</h1><p>Texte en xz. Retour &agrave; l'<a href='Accueil'>accueil</a>.</p></body></html>"]
mimes = ["text/html", "image/png"]
# entries sorted by (namespace, url)
ents = [("A", "Accueil", "Accueil", lambda: dirent(0, "A", 0, 0, "Accueil")),
        ("A", "Page_deux", "Page deux", lambda: dirent(0, "A", 0, 1, "Page_deux", "Page deux")),
        ("A", "Vieux", "Vieux", lambda: redirect("A", 1, "Vieux")),
        ("I", "logo.png", "logo.png", lambda: dirent(1, "I", 1, 0, "logo.png"))]
clusters = [cluster([h.encode() for h in html], 4), cluster([png], 1)]
mime_blob = b"".join(m.encode() + b"\0" for m in mimes) + b"\0"
dirents = [e[3]() for e in ents]
title_order = sorted(range(len(ents)), key=lambda i: (ents[i][0], ents[i][2]))

pos = 80
mime_pos = pos; pos += len(mime_blob)
url_ptr_pos = pos; pos += 8 * len(ents)
title_ptr_pos = pos; pos += 4 * len(ents)
cluster_ptr_pos = pos; pos += 8 * len(clusters)
dirent_pos = []
for d in dirents: dirent_pos.append(pos); pos += len(d)
cluster_pos = []
for c in clusters: cluster_pos.append(pos); pos += len(c)
checksum_pos = pos
header = struct.pack("<IHH16sIIQQQQIIQ", 0x044D495A, 5, 0, b"\1" * 16, len(ents), len(clusters), url_ptr_pos,
                     title_ptr_pos, cluster_ptr_pos, mime_pos, 0, 0xFFFFFFFF, checksum_pos)
out = header + mime_blob + b"".join(struct.pack("<Q", p) for p in dirent_pos) + \
      b"".join(struct.pack("<I", i) for i in title_order) + b"".join(struct.pack("<Q", p) for p in cluster_pos) + \
      b"".join(dirents) + b"".join(clusters) + b"\0" * 16
open(sys.argv[1], "wb").write(out)
print("wrote", sys.argv[1], len(out))
