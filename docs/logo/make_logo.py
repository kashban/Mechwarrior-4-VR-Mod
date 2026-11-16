# Writes the mod's logo (logo.svg: the badge; banner.svg: the badge beside the name) and renders them as PNG with
# a headless Edge or Chrome:  python docs/logo/make_logo.py
# The drawing is the mod's own; the lettering uses Bahnschrift (Windows), so render the PNGs on Windows.
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = "font-family=\"Bahnschrift, 'Arial Narrow', Arial, sans-serif\""
DEFS = '''  <defs>
    <linearGradient id="steel" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#3d4850"/>
      <stop offset="1" stop-color="#1a2025"/>
    </linearGradient>
    <linearGradient id="glass" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#143020"/>
      <stop offset="1" stop-color="#060b08"/>
    </linearGradient>
    <g id="cross" fill="none" stroke="#3dff55" stroke-width="5" stroke-linecap="square">
      <path d="M-34,-22 A40,40 0 0 0 -34,22"/>
      <path d="M34,-22 A40,40 0 0 1 34,22"/>
      <path d="M-34,0 H-12 M12,0 H34 M0,-40 V-14 M0,14 V40 M-8,-40 H8 M-8,40 H8"/>
      <circle r="2.5" fill="#3dff55" stroke="none"/>
    </g>
  </defs>
'''
# the badge in a box of 512 x 512: a mech's head wearing the headset's visor, the name below
BADGE = '''    <path d="M256,24 L462,142 V370 L256,488 L50,370 V142 Z" fill="#11161a" stroke="#8d98a1" stroke-width="7" stroke-linejoin="round"/>
    <path d="M256,48 L441,154 V358 L256,464 L71,358 V154 Z" fill="none" stroke="#2a3238" stroke-width="3"/>
    <g transform="translate(256,222) scale(0.9) translate(-256,-238)">
      <path d="M118,214 L92,194 V146 L122,160 Z M394,214 L420,194 V146 L390,160 Z" fill="#2a3238" stroke="#8d98a1" stroke-width="3.5" stroke-linejoin="round"/>
      <path d="M256,104 L352,128 L392,196 L378,306 L312,372 H200 L134,306 L120,196 L160,128 Z" fill="url(#steel)" stroke="#8d98a1" stroke-width="5.5" stroke-linejoin="round"/>
      <path d="M256,104 V150 M214,372 L226,330 H286 L298,372 M226,330 L204,298 M286,330 L308,298 M160,128 L196,160 M352,128 L316,160" stroke="#8d98a1" stroke-width="3" fill="none"/>
      <path d="M236,346 H276 M240,359 H272" stroke="#59646c" stroke-width="4"/>
      <path d="M150,292 H178 M334,292 H362" stroke="#59646c" stroke-width="4"/>
      <path d="M104,184 H408 L394,270 H302 L256,247 L210,270 H118 Z" fill="url(#glass)" stroke="#3dff55" stroke-width="4.5" stroke-linejoin="round"/>
      <use href="#cross" transform="translate(256,213) scale(0.64)"/>
      <path d="M136,204 H196 M136,219 H178 M316,204 H376 M334,219 H376" stroke="#3dff55" stroke-width="4" opacity="0.7"/>
    </g>
    <text x="256" y="424" font-size="50" font-weight="700" text-anchor="middle" letter-spacing="3"><tspan fill="#e6eaed">MW4</tspan><tspan fill="#3dff55">VR</tspan></text>
'''


def logo():
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512" %s>\n' % FONT) + DEFS + BADGE + '</svg>\n'


def banner():
    grid = ''.join('    <path d="M%d,0 V640" stroke="#141a1e" stroke-width="2"/>\n' % x for x in range(80, 1280, 80)) \
        + ''.join('    <path d="M0,%d H1280" stroke="#141a1e" stroke-width="2"/>\n' % y for y in range(80, 640, 80))
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1280 640" width="1280" height="640" %s>\n' % FONT) + DEFS \
        + '  <rect width="1280" height="640" fill="#0d1114"/>\n' + grid \
        + '  <g transform="translate(70,64)">\n' + BADGE + '  </g>\n' \
        + '  <text x="640" y="330" font-size="150" font-weight="700" letter-spacing="4"><tspan fill="#e6eaed">MW4</tspan><tspan fill="#3dff55">VR</tspan></text>\n' \
        + '  <path d="M644,366 H1196" stroke="#3dff55" stroke-width="4"/>\n' \
        + '  <text x="646" y="412" font-size="25" letter-spacing="4.6" fill="#8d98a1">MERCENARIES IN VIRTUAL REALITY</text>\n</svg>\n'


def browser():
    for p in (r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe', r'C:\Program Files\Microsoft\Edge\Application\msedge.exe',
              r'C:\Program Files\Google\Chrome\Application\chrome.exe'):
        if os.path.exists(p):
            return p
    return None


def render(exe, svg, png, w, h, box):
    """the SVG file as a PNG of w x h (its own box: box), on nothing"""
    page = os.path.join(tempfile.gettempdir(), 'mw4vr_logo.html')
    with open(svg, encoding='utf-8') as f:
        body = f.read().replace('width="%d" height="%d"' % box, 'width="%d" height="%d"' % (w, h), 1)
    with open(page, 'w', encoding='utf-8') as f:
        f.write('<!doctype html><html><body style="margin:0;background:transparent;overflow:hidden">' + body + '</body></html>')
    subprocess.run([exe, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--default-background-color=00000000', '--window-size=%d,%d' % (w, h),
                    '--screenshot=' + png, '--user-data-dir=' + os.path.join(tempfile.gettempdir(), 'mw4vr_logo_profile'), 'file:///' + page.replace('\\', '/')],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    for name, text in (('logo.svg', logo()), ('banner.svg', banner())):
        with open(os.path.join(HERE, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    exe = browser()
    if not exe:
        sys.exit('logo.svg and banner.svg written; no Edge or Chrome found to render the PNGs')
    for size in (1024, 512, 256):
        render(exe, os.path.join(HERE, 'logo.svg'), os.path.join(HERE, 'logo_%d.png' % size), size, size, (512, 512))
    render(exe, os.path.join(HERE, 'banner.svg'), os.path.join(HERE, 'banner_1280x640.png'), 1280, 640, (1280, 640))
    print('written:', ', '.join(sorted(n for n in os.listdir(HERE) if n.endswith(('.svg', '.png')))))


if __name__ == '__main__':
    main()
