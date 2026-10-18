#!/usr/bin/env python3
"""mkfont.py -- generate src/font/gfx_font.c: TEXTOUT's fonts 1-7 as 4-bit
anti-aliased bitmaps.

    python3 -I tools/mkfont.py DroidSans.ttf FreeSans.ttf > src/font/gfx_font.c
    (or `make fonts`, which downloads both and checks their SHA-256)

Glyphs come from Droid Sans (Apache License 2.0, the Android Open Source
Project), the typeface the HP Prime's own fonts match, with GNU FreeSans
(GPL-3.0-or-later with the font exception) for the symbols Droid Sans lacks.
Their licences are in src/font/.

The advance widths are not the fonts' own: they were measured on HP's Virtual
Calculator (TEXTOUT_P's answer for every character in fonts 1-7, 2026-10-18,
see notes/ppl.md), so TEXTOUT returns and lays out exactly as on the Prime.
Pixel sizes 10,12,...,22; fonts 1-2 are rendered unhinted (8x supersampled),
3-7 with FreeType's hinting, whichever matched the Prime's pixels better.

Needs Pillow (any Python with PIL) -- only to regenerate; the C file is
checked in.
"""
import sys
from PIL import Image, ImageFont

SIZES = [10, 12, 14, 16, 18, 20, 22]
BASE = [8, 10, 13, 16, 18, 20, 22]        # baseline below the top of the cell
CELL = [10, 12, 16, 20, 22, 24, 26]       # height of the background cell
SUPERSAMPLE = [True, True, False, False, False, False, False]

# Measured advance widths, font 1..7, one character per width: chr(48 + w).
W_ASCII = [  # 32..126
    '33466873335636346666666666336664966675577356597767655769555343546565653563353966664536575544646',
    '33587:83447737357777777777337775:777866884576:997976687;766454657676764673363;77775547696654747',
    '44698<:4448848458888888888448886<998:77::5687<::8:877:8<878454768787885784474=888866587:7765858',
    '446:9=;5559949469999999999449997>:::;88;;5798><<9<988;9>989565979997995894484>999967598<8876969',
    '557<:?=555::5:57::::::::::55:::8@;;;<:9<=68:9@==:=;99=:@:::575:7::;8;:69;5595@;:;;786;9=9986:6:',
    '558=;@>666;;5;58;;;;;;;;;;55;;;9A<<<>;:>>79<:A??<?<::>;B;;;686;8<;<9<;7:<55:5B<<<<897<:?::97;7;',
    '669><B?677<<6<68<<<<<<<<<<66<<<9C=>=?<;??7:=;C@@=@=;;?<C<<<787<9=<=:=<7;=66;6D====9:7=;@;;:8<8<']
W_LATIN1 = [  # 160..255
    '336666656835668446336573234577746666669655553333777777767777756655555585555533336666666666666565',
    '337777767:4677:55744768324469995777777:7666644448999999798888677666666:6666633337777777777777676',
    '448888878<5788<6685587943557:::6999999<877775555:::::::8:::::788777777<7888844448888888888888787',
    '449999989=5899=7795598;43568<<<7::::::>:88885555;<<<<<<9<;;;;89:999999=7999944449999999999999898',
    '55:::::9:?69::?78:66:9<54669===8;;;;;;?;::::6666<======:=====::;::::::?8::::5555:;:::::::;;;;9;9',
    '55;;;;;:<A7:;;A89;77<:=5477:???9<<<<<<A<;;;;7777>??????;?>>>>;<<;;;;;;A9;;;;5555<<<<<<<;<<<<<:<:',
    '66==<=<;=B7;=<B99<77=;>6578;@@@9======C=<<<<7777?@@@@@@<@????<==<<<<<<B:<<<<6666=======<=====;=;']
W_GREEK = [  # 913..969
    '66565577366975776:555858736656366656556635565466656567578',
    '77676689477:96987<666979937767477767557746676578767578699',
    '997878::588<:7::8>777;8;:4897958887866885778768987868:7:;',
    '::8989;<599><9<;9@988<9<<49:8:5:9:897799588:879:98979;8<<',
    ';;9:::==6::@=:=<:B:9:>:>=5:;9;6;;;9:88;:699;98:;:9;8;=9=>',
    '<<:;;;>?7<;A?;?><D;:;?;??6;<9<7<<<:<99<;7::<:9<=<:<9<>:??',
    '=>;<<<?@7=<C@<@?=F<;<A<A@6<=:>7===;=::=<7;;=;:=>=;=:=?<@A']
SYMBOLS = [8722, 7431, 57347, 8520, 8592, 8593, 8594, 8595, 8596, 8658, 8660, 8734, 8730, 8747,
           8721, 8719, 8706, 8710, 8776, 8800, 8804, 8805, 8712, 8736, 9654, 9650, 9660, 9664,
           8226, 8230, 8216, 8217, 8220, 8221, 8211, 8212, 8364, 8304, 8308, 8309, 8310, 8311,
           8312, 8313, 8315, 8320, 8321, 8322, 8323, 8324, 8325, 8326, 8327, 8328, 8329, 8709,
           8745, 8746, 8743, 8744, 8801, 8733, 8704, 8707, 8853, 8855, 8901, 8242, 8243, 9632,
           9679, 9633, 9675, 8486, 8467, 8491, 8706, 8711, 8869, 8741]
W_SYM = [
    '543::5:5:::8:5:::66666::66664822445:6333333334333333333:::::67::::42466667:::::8',
    '654<<6<6<<<:<6<<<77777<<77775:22446<7444444435444444444<<<<<79<<<<53577779<<<<<:',
    '765>>7>7>>><?7>>>88888>>88886;22557>8555555545555555555>>>>>8:>>>>6368888:>>>>><',
    '875@@8@8@@@=A8@@@99999@@::::7=33668@9656666646555566666@@@@@9;@@@@746::::<@@@@@>',
    '986BB9B9BBB?C9BBB:::::BB;;;;8>33779B:666666657666666666BBBBB:=BBBB747;;;;=BBBBB?',
    '997DD:D:DDD@E:DDD;;;;;DD<<<<9@4477:D;777777758777777777DDDDD;>DDDD858<<<<?DDDDDA',
    '::7FF;F;FFFBG;FFF=<<<<FF====:B4488;F<878888868777788888FFFFF<@FFFF959====@FFFFFC']

# characters drawn with another code point's glyph
SUBST = {0x2D: 0x2212,     # the Prime draws '-' as a minus, as wide as '+'
         0xE003: 0x69,     # the Prime's imaginary unit
         0x2148: 0x69}
# characters made from another glyph scaled and moved (the Prime's own
# shapes): code -> (source, scale, raise in em, left edge in em from the pen)
SYNTH = {0x2212: (0x2D, 1.15, 0.18, 0.09),   # its minus is a short raised dash: "-5" shows as a superscript minus
         0x1D07: (0x45, 0.76, 0.0, 0.0)}     # small capital E, x-height tall, as in 2.5E-3


def widths():
    w = {}
    def put(codes, table):
        for f in range(7):
            for c, ch in zip(codes, table[f]):
                w.setdefault(c, [0] * 7)[f] = ord(ch) - 48
    put(range(32, 127), W_ASCII)
    put(range(160, 256), W_LATIN1)
    put([c for c in range(913, 970)], W_GREEK)
    put(SYMBOLS, W_SYM)
    w.pop(930, None)        # U+03A2 is not a character
    return w


def cmap_of(path):
    from fontTools.ttLib import TTFont
    return set(TTFont(path).getBestCmap())


def render(path, cp, size, ss, scale=1.0):
    """-> (image L, x, y) of the ink box relative to the pen on the baseline"""
    ch = chr(cp)
    if not ss:
        f = ImageFont.truetype(path, size * scale)
        m, (ox, oy) = f.getmask2(ch, mode='L', anchor='ls')
        if not m.size[0] or not m.size[1]:
            return None, 0, 0
        return Image.frombytes('L', m.size, bytes(m)), ox, oy
    k = 8
    f = ImageFont.truetype(path, size * k * scale)
    m, (ox, oy) = f.getmask2(ch, mode='L', anchor='ls')
    if not m.size[0] or not m.size[1]:
        return None, 0, 0
    oy -= 2                                     # a quarter pixel up matched the Prime best
    im = Image.frombytes('L', m.size, bytes(m))
    x0, y0 = (ox // k) * k, (oy // k) * k
    bw = ((ox + m.size[0] - x0 + k - 1) // k) * k
    bh = ((oy + m.size[1] - y0 + k - 1) // k) * k
    big = Image.new('L', (bw, bh), 0)
    big.paste(im, (ox - x0, oy - y0))
    return big.resize((bw // k, bh // k), Image.BOX), x0 // k, y0 // k


def advance(path, cp, size):
    return round(ImageFont.truetype(path, size).getlength(chr(cp)))


def main():
    droid, free = sys.argv[1], sys.argv[2]
    dcm, fcm = cmap_of(droid), cmap_of(free)
    W = widths()
    cps = sorted(W)
    out = []
    out.append('/* gfx_font.c -- GENERATED by tools/mkfont.py; do not edit.\n'
               ' *\n'
               ' * TEXTOUT fonts 1-7 (10..22 px), 4-bit anti-aliased glyphs rendered from\n'
               ' * Droid Sans (Copyright 2005-2008 The Android Open Source Project, Apache\n'
               ' * License 2.0, see LICENSE-DroidSans.txt) and GNU FreeSans (GPL-3.0-or-later\n'
               ' * with the font exception, see LICENSE-FreeFont.txt).  Advance widths are\n'
               ' * the HP Prime\'s, measured.  Glyph bitmaps: rows of w*h 4-bit coverage\n'
               ' * values packed high nibble first, continuous across rows. */\n'
               '#include "../gfx_font.h"\n')
    out.append('const uint16_t gfx_font_cp[%d] = {' % (len(cps) + 1))
    allcp = [0] + cps
    for i in range(0, len(allcp), 12):
        out.append('    ' + ' '.join('0x%04X,' % c for c in allcp[i:i + 12]))
    out.append('};\nconst int gfx_font_ncp = %d;\n' % len(allcp))
    total = 0
    for f in range(7):
        size = SIZES[f]
        recs, data = [], bytearray()
        for cp in allcp:
            adv = W[cp][f] if cp else max(3, size // 2)
            nib = []
            if cp == 0:                      # missing character: a hollow box
                w, h = adv - 1, (size * 7) // 10
                x, y = 0, -h
                for r in range(h):
                    for c in range(w):
                        nib.append(15 if r in (0, h - 1) or c in (0, w - 1) else 0)
            else:
                src = SUBST.get(cp, cp)
                scale, lift, atpen = 1.0, 0.0, None
                if cp in SYNTH:
                    src, scale, lift, atpen = SYNTH[cp]
                path = droid if src in dcm else free
                im, x, y = render(path, src, size, SUPERSAMPLE[f], scale)
                y -= round(lift * size)
                if im is None:
                    w = h = 0
                else:
                    bbox = im.getbbox()
                    if not bbox:
                        w = h = 0; x = y = 0
                    else:
                        im = im.crop(bbox)
                        x += bbox[0]; y += bbox[1]
                        w, h = im.size
                        fa = advance(path, src, size)
                        if atpen is not None:
                            x = round(atpen * size)
                        elif abs(adv - fa) >= 2:        # centre in the Prime's advance
                            x += (adv - fa) // 2
                        nib = [(v * 15 + 127) // 255 for v in im.getdata()]
            if len(nib) % 2:
                nib.append(0)
            off = len(data)
            for i in range(0, len(nib), 2):
                data.append(nib[i] << 4 | nib[i + 1])
            assert off < 65536 and w < 256 and h < 256 and -128 <= x < 128 and -128 <= y < 128
            recs.append((off, adv, w, h, x, y))
        total += len(data) + 8 * len(recs)
        out.append('static const uint8_t data%d[%d] = {' % (f + 1, max(1, len(data))))
        for i in range(0, len(data), 24):
            out.append('    ' + ''.join('%d,' % b for b in data[i:i + 24]))
        out.append('};')
        out.append('static const gfx_glyph glyphs%d[%d] = {' % (f + 1, len(recs)))
        for i in range(0, len(recs), 6):
            out.append('    ' + ' '.join('{%d,%d,%d,%d,%d,%d},' % r for r in recs[i:i + 6]))
        out.append('};\n')
    out.append('const gfx_font gfx_fonts[7] = {')
    for f in range(7):
        out.append('    { %d, %d, %d, glyphs%d, data%d },' % (SIZES[f], BASE[f], CELL[f], f + 1, f + 1))
    out.append('};')
    sys.stdout.write('\n'.join(out) + '\n')
    sys.stderr.write('mkfont: %d characters, %d bytes of glyph data and records\n' % (len(allcp), total))


if __name__ == '__main__':
    main()
