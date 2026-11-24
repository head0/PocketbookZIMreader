#!/usr/bin/env python3
"""Builds a small ZIM archive used to exercise the reader (needs: pip install libzim pillow)."""
import io, os, sys
from libzim.writer import Creator, Item, StringProvider, Hint
from PIL import Image, ImageDraw

class It(Item):
    def __init__(s, path, title, mime, data, front=False):
        super().__init__(); s.p, s.t, s.m, s.d, s.f = path, title, mime, data, front
    def get_path(s): return s.p
    def get_title(s): return s.t
    def get_mimetype(s): return s.m
    def get_contentprovider(s): return StringProvider(s.d)
    def get_hints(s): return {Hint.FRONT_ARTICLE: s.f, Hint.COMPRESS: s.m.startswith("text/")}

def img(fmt, w=440, h=300):
    im = Image.new("RGB", (w, h), "white"); d = ImageDraw.Draw(im)
    for i in range(0, w, 20): d.line([(i, 0), (w - i, h)], fill=(i % 255, 80, 160), width=3)
    d.ellipse([w//4, h//4, 3*w//4, 3*h//4], outline="black", width=6); d.text((10, 10), fmt, fill="black")
    b = io.BytesIO(); im.save(b, fmt); return b.getvalue()

LOREM = ("Le chat domestique est un mammif&egrave;re carnivore de la famille des f&eacute;lid&eacute;s. "
         "Il est l&rsquo;un des principaux animaux de compagnie et compte aujourd&rsquo;hui une "
         "cinquantaine de races diff&eacute;rentes reconnues par les instances de certification. ")

def page(title, body):
    return ("<!DOCTYPE html><html><head><meta charset='utf-8'><title>%s</title>"
            "<style>p{color:red}</style><script>var a='<p>nope</p>';</script></head><body>%s</body></html>" % (title, body))

articles = {
 "Accueil": ("Accueil", "<h1>Encyclop&eacute;die de test</h1><p>Bienvenue. Voir <a href='Chat'>Chat</a>, "
   "<a href='./Chien'>Chien</a>, <a href='%C3%89t%C3%A9'>&Eacute;t&eacute;</a>, <a href='Sous/Page'>une sous-page</a>, "
   "<a href='Felin'>une redirection</a>, <a href='Absent'>un lien mort</a> et "
   "<a href='https://example.org'>un lien externe</a>.</p>"),
 "Chat": ("Chat", "<h1>Chat</h1><p><b>Le chat</b> (<i>Felis catus</i>) " + LOREM * 3 + "<sup><a href='#cite1'>[1]</a></sup></p>"
   "<table class='infobox'><tr><th colspan=2>Chat domestique</th></tr><tr><th>R&egrave;gne</th><td>Animalia</td></tr>"
   "<tr><th>Classe</th><td>Mammalia</td></tr></table>"
   "<figure><img src='I/chat.png' width='220' height='150'><figcaption>Un chat (PNG)</figcaption></figure>"
   "<h2>Description</h2><p>" + LOREM * 4 + "</p><ul><li>Premier point avec un <a href='Chien'>lien</a></li>"
   "<li>Second point assez long pour passer &agrave; la ligne suivante sans aucune difficult&eacute; particuli&egrave;re</li>"
   "<li>Niveau deux :<ul><li>sous-point</li></ul></li></ul><ol><li>Un</li><li>Deux</li></ol>"
   "<img src='I/chat.jpg'><p>Image WebP :</p><img src='I/chat.webp' width='220' height='150'>"
   "<h3>Code</h3><pre>for (i = 0; i &lt; n; i++) {\n    miaou(i);\n}</pre>"
   "<p>Formule <span class='mwe-math-element'><span style='display: none'><math><mi>x</mi></math></span>"
   "<img class='mwe-math-fallback-image-inline' src='I/x.svg' alt='{\\displaystyle E=mc^{2}}'></span> en ligne.</p>"
   "<div class='navbox'>NAVBOX INVISIBLE</div><span class='mw-editsection'>[modifier]</span>"
   "<h2>Histoire</h2>" + ("<p>" + LOREM * 5 + "</p>") * 12 +
   "<p>URL tr&egrave;s longue : https://exemple.org/un/chemin/extremement/long/qui/ne/tient/pas/sur/une/seule/ligne/de/texte/meme/en/petit.html</p>"),
 "Chien": ("Chien", "<h1>Chien</h1><p>Le chien. Retour au <a href='Chat'>chat</a>.</p>"),
 "Été": ("Été", "<h1>&Eacute;t&eacute;</h1><p>Saison chaude.</p>"),
 "Sous/Page": ("Sous-page", "<h1>Sous-page</h1><p>Lien relatif vers <a href='../Chat'>Chat</a> et image "
   "<img src='../I/chat.png' width='110' height='75'></p>"),
}
for i in range(120):
    articles["Chapitre_%03d" % i] = ("Chapitre %03d" % i, "<h1>Chapitre %d</h1><p>%s</p>" % (i, LOREM))

out = sys.argv[1]
with Creator(out).config_indexing(False, "fra") as c:
    c.set_mainpath("Accueil")
    # BIG=1: push the articles past the 4 GiB mark to exercise 64-bit offsets / split archives
    if os.environ.get("BIG"):
        for i in range(45):
            c.add_item(It("big/%02d.bin" % i, "", "application/octet-stream", os.urandom(100 << 20)))
    for path, (title, body) in articles.items():
        c.add_item(It(path, title, "text/html", page(title, body), True))
    c.add_item(It("I/chat.png", "", "image/png", img("PNG")))
    c.add_item(It("I/chat.jpg", "", "image/jpeg", img("JPEG")))
    c.add_item(It("I/chat.webp", "", "image/webp", img("WEBP")))
    c.add_redirection("Felin", "Félin", "Chat", {Hint.FRONT_ARTICLE: True})
    for k, v in {"Title": "Encyclopédie de test", "Language": "fra", "Name": "test", "Creator": "t",
                 "Publisher": "t", "Date": "2026-01-01", "Description": "test"}.items():
        c.add_metadata(k, v)
print("wrote", out)
