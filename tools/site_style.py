#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""ANATOMIX VOXINATOR's look for the site fm1-base's tools/make_site.py builds (run it after make_site.py):

  tools/site_style.py SITE_DIR

- install/index.html (fm1-base's installer): site.css and app/site/install.css in place of its style, and the
  site's header above it; its markup and script stay as they are;
- emu/index.html (fm1-base's emulator): site.css and app/site/emu.css in place of its style (its Barlow stays for the
  panel's printed labels), the site's
  header and an introduction in place of its title, and a "where to start" row under the panel; its panel, markup
  and script stay as they are;
- index.html and manual/index.html: app/site/site.css (with its fonts) in place of fm1-base's style, a class on
  <body> for each page, and a "Controls" link in the site's header (to the manual's control map);
- manual/index.html: the control map (app/site/controlmap.html: the pages, the knobs, the panel) after the heading
  of the section "The control map", in place of that section's text (the Markdown's text
  is for readers on GitHub); and the contents follow the reading (the section on screen marked).

The pages keep everything else make_site.py gave them: their text, links, images, tables and the contents."""
import re
import sys
from pathlib import Path

APP = Path(__file__).resolve().parents[1] / "app"
FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com">\n'
         '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>\n'
         '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600;700'
         '&family=Barlow:wght@400;500;600&family=IBM+Plex+Mono:wght@400;500;600&display=swap">\n')
SPY = """<script>
(function () {                         /* the contents follow the reading: the section on screen marked */
  var links = {}, nav = document.querySelector("nav.contents");
  if (!nav || !("IntersectionObserver" in window)) return;
  nav.querySelectorAll("a[href^='#']").forEach(function (a) { links[a.getAttribute("href").slice(1)] = a; });
  var seen = new Set();
  var io = new IntersectionObserver(function (es) {
    es.forEach(function (e) { if (e.isIntersecting) seen.add(e.target.id); else seen.delete(e.target.id); });
    var first = Object.keys(links).find(function (id) { return seen.has(id); });
    if (!first) return;
    Object.keys(links).forEach(function (id) {
      if (id === first) links[id].setAttribute("aria-current", "true"); else links[id].removeAttribute("aria-current");
    });
  }, { rootMargin: "-80px 0px -55% 0px" });
  Object.keys(links).forEach(function (id) { var h = document.getElementById(id); if (h) io.observe(h); });
})();
</script>
"""


def restyle(path, cls, controls_href, extra=""):
    s = path.read_text(encoding="utf-8")
    css = (APP / "site" / "site.css").read_text(encoding="utf-8") + extra
    s, n = re.subn(r"<style>.*?</style>", lambda m: FONTS + "<style>" + css + "</style>", s, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"{path}: no <style> to replace")
    s = s.replace("<body>", f'<body class="{cls}">', 1)
    nav = re.search(r'(<header class="site">.*?<nav>)(.*?)(</nav>)', s, re.S)
    if nav:                                          # "Controls" after "Manual"
        links = re.sub(r'(<a href="[^"]*manual/"[^>]*>Manual</a>)', r'\1<a href="' + controls_href + '">Controls</a>',
                       nav.group(2), count=1)
        s = s[:nav.start(2)] + links + s[nav.end(2):]
    return s


def header(current):
    conf = (APP / "fm1.toml").read_text(encoding="utf-8")
    name = re.search(r'^name\s*=\s*"([^"]+)"', conf, re.M)
    repo = re.search(r'^repo\s*=\s*"([^"]+)"', conf, re.M)
    links = [("../emu/", "Emulator"), ("../install/", "Install"), ("../manual/", "Manual"), ("../manual/#controls", "Controls")]
    if repo:
        links.append((repo.group(1), "Source"))
    return ('<div class="page"><header class="site"><a class="home" href="../">' + (name.group(1) if name else "ANATOMIX VOXINATOR") +
            "</a><nav>" + "".join(f'<a href="{h}"' + (' aria-current="page"' if t == current else "") + f">{t}</a>"
                                  for h, t in links) + "</nav></header></div>\n")


EMU_START = """<div class="start-here" aria-label="Where to start">
  <div><b>Give it a microphone</b><p>The browser's emulator takes its voice from the microphone, if you allow it: the instrument's
  INPUT screen has the level. A real FM-1 needs a USB microphone on a <b>powered hub</b>: the port hands out no power.</p></div>
  <div><b>Sing, then turn</b><p>The folds on the screen vibrate at the pitch you sing, and the note appears in the header.
  Turn K1–K4 and the anatomy answers: the tract shortens with the formants, the airflow speeds up with the level.</p></div>
  <div><b>Find your way</b><p>Each button opens a page; ALGORITHM switches between the two engines. <span class="k">ARP</span> is the other
  half. All of it is in the <a href="../manual/#controls">control map</a>.</p></div>
</div>
"""


def emu(site):
    """the emulator: ANATOMIX's style and fonts, the site's header, an introduction, a row of where to start"""
    page = site / "emu" / "index.html"
    if not page.exists():
        return
    s = restyle(page, "emu", "", (APP / "site" / "emu.css").read_text(encoding="utf-8"))
    m = re.search(r"<header>\s*<h1>([^<]*)</h1>\s*</header>\s*<main>", s)
    if not m:
        raise SystemExit(f"{page}: no <header><h1> then <main> to replace")
    s = (s[:m.start()] + header("Emulator") + '<main>\n<div class="intro"><h1>' + m.group(1) + " in the browser</h1><p>The "
         "same code the FM-1 runs, with sound. Play it with the mouse, a touch screen or the keyboard.</p></div>" + s[m.end():])
    s = s.replace('<details class="speak" id="speak">', EMU_START + '<details class="speak" id="speak">', 1)
    page.write_text(s, encoding="utf-8")


def install(site):
    """the installer: ANATOMIX's style, and the site's header (the same pages as the manual's) above its main"""
    page = site / "install" / "index.html"
    if not page.exists():
        return
    s = restyle(page, "install", "", (APP / "site" / "install.css").read_text(encoding="utf-8"))
    s = s.replace('<body class="install">', '<body class="install">\n' + header("Install"), 1)
    page.write_text(s, encoding="utf-8")


def main(site):
    site = Path(site)
    install(site)
    emu(site)
    home = site / "index.html"
    if home.exists():
        home.write_text(restyle(home, "home", "manual/#controls"), encoding="utf-8")
    man = site / "manual" / "index.html"
    if not man.exists():
        print(f"site_style: {site}: front page restyled (no manual)")
        return
    s = restyle(man, "manual", "#controls")
    if '<header class="site">' not in s:                 # the manual's title was not the firmware's name:
        s = re.sub(r'<a class="home"[^>]*>[^<]*</a>\s*', "", s, count=1)   # its own way home is not needed
        s = re.sub(r"(<main[^>]*>)", lambda m: header("Manual") + m.group(1), s, count=1)
    h = re.search(r'<h2 id="(s\d+)"><span class="num">\d+</span>The control map</h2>', s)
    if not h:
        raise SystemExit(f"{man}: no section \"The control map\" (app/docs/MANUAL.md)")
    end = re.compile(r"<h2 |</main>").search(s, h.end())
    widget = (APP / "site" / "controlmap.html").read_text(encoding="utf-8")
    widget = re.sub(r"<!--.*?-->\s*", "", widget, count=1, flags=re.S)
    s = (s[:h.start()] + '<span id="controls"></span>' + s[h.start():h.end()] + "\n" + widget + "\n" + s[end.start():])
    s = s.replace("</body>", SPY + "</body>", 1)
    man.write_text(s, encoding="utf-8")
    print(f"site_style: {site}: the front page and the manual restyled, the control map in section {h.group(1)}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
