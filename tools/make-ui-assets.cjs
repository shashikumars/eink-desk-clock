// Generates the bitmap fonts and icons for the clock's screen layout:
//   ../src/ui_fonts.h   Barlow Condensed, Barlow, IBM Plex Mono and Outfit at the sizes the layout uses
//   ../src/ui_icons.h   Material Symbols Sharp weather icons
//
// Usage (from this folder):  npm install   then   node make-ui-assets.cjs
// Fonts: SIL Open Font License 1.1. Icons: Apache License 2.0.

const fs = require('fs');
const path = require('path');
const opentype = require('opentype.js');
const svgpath = require('svgpath');

const SS = 4;              // supersampling per axis
const INK_COVERAGE = 0.45; // share of a pixel that must be covered to become ink

const ASCII = Array.from({ length: 95 }, (_, i) => String.fromCharCode(32 + i)).join('') + '°·';
const DIGITS = '0123456789';

// name, font file, size in px (same as CSS font-size), characters
const FONTS = [
  ['FONT_CLOCK', 'barlow-condensed-700', 238, DIGITS + ':'],
  ['FONT_TEMP', 'barlow-condensed-600', 84, DIGITS + '-°'],
  ['FONT_DAY_HI', 'barlow-condensed-600', 26, DIGITS + '-° '],
  ['FONT_DAY_LO', 'barlow-condensed-500', 26, DIGITS + '-° '],
  ['FONT_TITLE', 'barlow-700', 20, ASCII],
  ['FONT_LABEL', 'barlow-700', 18, ASCII],
  ['FONT_BODY', 'barlow-500', 17, ASCII],
  ['FONT_SMALL', 'barlow-500', 16, ASCII],
  ['FONT_MONO', 'ibm-plex-mono-600', 16, ASCII],
  ['FONT_MONO_AMPM', 'ibm-plex-mono-600', 20, 'AMP'],
  ['FONT_MONO_SMALL', 'ibm-plex-mono-600', 13, ASCII],
  ['FONT_MONO_FOOT', 'ibm-plex-mono-500', 13, ASCII],
  // Clock-only layout (design 2a/2b)
  ['FONT_CLOCK_XL', 'barlow-condensed-600', 330, DIGITS + ':'],
  ['FONT_DATE_XL', 'barlow-600', 38, ASCII],
  ['FONT_YEAR_XL', 'barlow-condensed-500', 38, DIGITS],
  // Analog dial numerals (12, 3, 6, 9)
  ['FONT_DIAL', 'barlow-condensed-600', 40, DIGITS],
  // Full-screen analog face: thin 12 and 6, date and temperature
  ['FONT_FACE_NUM', 'outfit-300', 120, DIGITS],
  ['FONT_FACE_INFO', 'outfit-500', 30, ASCII],
];

// icon name, sizes in px
const ICONS = [
  ['sunny', [64, 30]], ['bedtime', [64, 30, 20]], ['partly_cloudy_day', [64, 30]],
  ['partly_cloudy_night', [64, 30]], ['cloud', [64, 30]], ['foggy', [64, 30]],
  ['rainy_light', [64, 30]], ['rainy', [64, 30]], ['rainy_heavy', [64, 30]],
  ['weather_snowy', [64, 30]], ['thunderstorm', [64, 30]], ['wb_twilight', [20]],
  ['calendar_today', [30]],
];

// ---------------------------------------------------------------------------
// Rasterizer: polygons (arrays of [x, y], y down) -> packed 1-bit rows, MSB first
// ---------------------------------------------------------------------------
function rasterize(polys, x0, y0, w, h) {
  const edges = [];
  for (const poly of polys) {
    for (let i = 0; i < poly.length; i++) {
      const [ax, ay] = poly[i], [bx, by] = poly[(i + 1) % poly.length];
      if (ay !== by) edges.push({ ax, ay, bx, by, dir: by > ay ? 1 : -1 });
    }
  }
  const cov = new Uint16Array(w * h);
  for (let py = 0; py < h; py++) {
    for (let sy = 0; sy < SS; sy++) {
      const yy = y0 + py + (sy + 0.5) / SS;
      const xs = [];
      for (const e of edges) {
        if (yy < Math.min(e.ay, e.by) || yy >= Math.max(e.ay, e.by)) continue;
        xs.push({ x: e.ax + (yy - e.ay) * (e.bx - e.ax) / (e.by - e.ay), dir: e.dir });
      }
      xs.sort((a, b) => a.x - b.x);
      let wind = 0;
      for (let i = 0; i < xs.length - 1; i++) {
        wind += xs[i].dir;
        if (wind === 0) continue;
        const a = (xs[i].x - x0) * SS, b = (xs[i + 1].x - x0) * SS;
        for (let s = Math.max(0, Math.ceil(a - 0.5)); s <= Math.min(w * SS - 1, Math.floor(b - 0.5)); s++) {
          cov[py * w + Math.floor(s / SS)]++;
        }
      }
    }
  }
  const rowBytes = Math.ceil(w / 8), out = [];
  for (let y = 0; y < h; y++) {
    for (let bx = 0; bx < rowBytes; bx++) {
      let b = 0;
      for (let bit = 0; bit < 8; bit++) {
        const x = bx * 8 + bit;
        if (x < w && cov[y * w + x] >= SS * SS * INK_COVERAGE) b |= 0x80 >> bit;
      }
      out.push(b);
    }
  }
  return out;
}

const N = 12; // line segments per curve
function curvesToPolys(cmds) {
  const polys = [];
  let cur = [], x = 0, y = 0, sx = 0, sy = 0;
  const close = () => { if (cur.length > 2) polys.push(cur); cur = []; };
  for (const c of cmds) {
    switch (c[0]) {
      case 'M': close(); x = sx = c[1]; y = sy = c[2]; cur.push([x, y]); break;
      case 'L': x = c[1]; y = c[2]; cur.push([x, y]); break;
      case 'H': x = c[1]; cur.push([x, y]); break;
      case 'V': y = c[1]; cur.push([x, y]); break;
      case 'Q':
        for (let i = 1; i <= N; i++) { const t = i / N, u = 1 - t;
          cur.push([u * u * x + 2 * u * t * c[1] + t * t * c[3], u * u * y + 2 * u * t * c[2] + t * t * c[4]]); }
        x = c[3]; y = c[4]; break;
      case 'C':
        for (let i = 1; i <= N; i++) { const t = i / N, u = 1 - t;
          cur.push([u * u * u * x + 3 * u * u * t * c[1] + 3 * u * t * t * c[3] + t * t * t * c[5],
                    u * u * u * y + 3 * u * u * t * c[2] + 3 * u * t * t * c[4] + t * t * t * c[6]]); }
        x = c[5]; y = c[6]; break;
      case 'Z': x = sx; y = sy; close(); break;
      default: throw new Error('unsupported path command ' + c[0]);
    }
  }
  close();
  return polys;
}

// opentype path commands -> the same command arrays
const otCmds = (p) => p.commands.map(c =>
  c.type === 'M' ? ['M', c.x, c.y] : c.type === 'L' ? ['L', c.x, c.y] :
  c.type === 'Q' ? ['Q', c.x1, c.y1, c.x, c.y] : c.type === 'C' ? ['C', c.x1, c.y1, c.x2, c.y2, c.x, c.y] : ['Z']);

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------
const hexRows = (bytes) => {
  const rows = [];
  for (let i = 0; i < bytes.length; i += 20) rows.push('  ' + bytes.slice(i, i + 20).map(b => '0x' + b.toString(16).padStart(2, '0')).join(', '));
  return rows.join(',\n');
};
const cname = (s) => s.replace(/[^A-Za-z0-9]/g, '_');

let fontsOut = `// Generated by tools/make-ui-assets.cjs. Do not edit by hand.
// Barlow, Barlow Condensed, IBM Plex Mono: SIL Open Font License 1.1.
#pragma once
#include <Arduino.h>

struct UiGlyph {
  uint32_t offset;    // into the font's bitmap; rows padded to whole bytes, MSB first
  uint8_t width, height;
  int8_t xOffset;     // cursor to left edge of the bitmap
  int16_t yOffset;    // baseline to top edge of the bitmap (negative = above)
  uint8_t xAdvance;
};
struct UiFont {
  const uint8_t *bitmap;
  const UiGlyph *glyphs;
  const uint16_t *codes;  // Unicode code point of each glyph
  uint8_t count;
  uint8_t capHeight;      // height of capitals/digits in px
};
`;

let totalFontBytes = 0;
for (const [name, file, size, chars] of FONTS) {
  const font = opentype.loadSync(path.join(__dirname, 'fonts', file + '.ttf'));
  const bytes = [], glyphs = [], codes = [];
  for (const ch of Array.from(new Set(chars))) {
    const g = font.charToGlyph(ch);
    const adv = Math.round(g.advanceWidth * size / font.unitsPerEm);
    const p = g.getPath(0, 0, size);
    const bb = p.getBoundingBox();
    let x0 = 0, y0 = 0, w = 0, h = 0, data = [];
    if (isFinite(bb.x1) && bb.x2 > bb.x1) {
      x0 = Math.floor(bb.x1); y0 = Math.floor(bb.y1);
      w = Math.ceil(bb.x2) - x0; h = Math.ceil(bb.y2) - y0;
      data = rasterize(curvesToPolys(otCmds(p)), x0, y0, w, h);
    }
    glyphs.push(`  {${bytes.length}, ${w}, ${h}, ${x0}, ${y0}, ${adv}},  // U+${ch.codePointAt(0).toString(16).padStart(4, '0')}`);
    codes.push(ch.codePointAt(0));
    bytes.push(...data);
  }
  totalFontBytes += bytes.length;
  const cap = Math.round((font.tables.os2.sCapHeight || font.unitsPerEm * 0.7) * size / font.unitsPerEm);
  fontsOut += `
// ${file} ${size}px
static const uint8_t ${name}_BITMAP[] PROGMEM = {
${hexRows(bytes.length ? bytes : [0])}
};
static const UiGlyph ${name}_GLYPHS[] = {
${glyphs.join('\n')}
};
static const uint16_t ${name}_CODES[] = {${codes.join(', ')}};
static const UiFont ${name} = {${name}_BITMAP, ${name}_GLYPHS, ${name}_CODES, ${codes.length}, ${cap}};
`;
}
fs.writeFileSync(path.join(__dirname, '..', 'src', 'ui_fonts.h'), fontsOut);

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------
let iconsOut = `// Generated by tools/make-ui-assets.cjs. Do not edit by hand.
// Material Symbols Sharp (weight 400): Apache License 2.0.
#pragma once
#include <Arduino.h>

struct UiIcon {
  const uint8_t *bitmap;  // size x size, rows padded to whole bytes, MSB first
  uint8_t size;
};
`;
let totalIconBytes = 0;
for (const [name, sizes] of ICONS) {
  const svg = fs.readFileSync(path.join(__dirname, 'icons', name + '.svg'), 'utf8');
  const d = svg.match(/ d="([^"]+)"/)[1];
  for (const size of sizes) {
    // viewBox is 0 -960 960 960: move to 0..960 and scale to the icon size
    // svgpath applies transforms lazily, so re-parse the transformed string to read its segments
    const moved = svgpath(d).abs().unshort().unarc().translate(0, 960).scale(size / 960).toString();
    const segs = svgpath(moved).abs().segments;
    const data = rasterize(curvesToPolys(segs), 0, 0, size, size);
    totalIconBytes += data.length;
    const id = `ICON_${cname(name).toUpperCase()}_${size}`;
    iconsOut += `static const uint8_t ${id}_BITMAP[] PROGMEM = {\n${hexRows(data)}\n};\nstatic const UiIcon ${id} = {${id}_BITMAP, ${size}};\n`;
  }
}
fs.writeFileSync(path.join(__dirname, '..', 'src', 'ui_icons.h'), iconsOut);

console.log(`Wrote src/ui_fonts.h (${FONTS.length} fonts, ${totalFontBytes} bytes) and ` +
            `src/ui_icons.h (${totalIconBytes} bytes)`);
