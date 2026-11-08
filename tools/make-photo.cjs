// Converts a photo for the 7.5" (B) e-paper (800 x 480, black/white/red).
//
//   node make-photo.cjs <photo.jpg> [focusY]
//
// focusY (0..1, default 0.4) picks which part of the photo is kept when cropping to 800x480.
// Writes:
//   ../photo/preview_bw.png    how it looks in black and white
//   ../photo/preview_bwr.png   how it looks with red as well
//   ../src/photo/photo_data.h  the black/white/red version, for the "photo" firmware

const fs = require('fs');
const path = require('path');
const Jimp = require('jimp');

const W = 800, H = 480;
const GAMMA = Number(process.env.GAMMA || 1.6);  // above 1 darkens mid-tones
// Colours as they look on the actual panel, used for the previews
const PAPER = [228, 227, 221], INK = [28, 28, 30], RED_INK = [178, 40, 31];

async function main() {
  const [src, focusArg] = process.argv.slice(2);
  if (!src) { console.log('usage: node make-photo.cjs <photo.jpg> [focusY]'); process.exit(1); }
  const focusY = focusArg ? Number(focusArg) : 0.4;

  const img = await Jimp.read(src);
  // Crop to the panel's shape, then scale
  const scale = Math.max(W / img.bitmap.width, H / img.bitmap.height);
  img.resize(Math.round(img.bitmap.width * scale), Math.round(img.bitmap.height * scale), Jimp.RESIZE_BICUBIC);
  const x0 = Math.round((img.bitmap.width - W) / 2);
  const y0 = Math.round((img.bitmap.height - H) * focusY);
  img.crop(x0, y0, W, H);
  const d = img.bitmap.data;
  const lum = new Float32Array(W * H), redness = new Float32Array(W * H);
  for (let i = 0; i < W * H; i++) {
    const r = d[i * 4], g = d[i * 4 + 1], b = d[i * 4 + 2];
    lum[i] = 0.299 * r + 0.587 * g + 0.114 * b;
    redness[i] = r - Math.max(g, b);   // high for red/orange, low for skin, grey, green
  }
  const tone = toneMap(lum);

  const outDir = path.join(__dirname, '..', 'photo');
  fs.mkdirSync(outDir, { recursive: true });

  const bw = dither(tone, null);
  const bwr = dither(tone, redness);
  await savePreview(bw, path.join(outDir, 'preview_bw.png'));
  await savePreview(bwr, path.join(outDir, 'preview_bwr.png'));
  writeHeader(bwr, path.join(__dirname, '..', 'src', 'photo', 'photo_data.h'), path.basename(src));
  console.log('Wrote photo/preview_bw.png, photo/preview_bwr.png and src/photo/photo_data.h');
}

// Stretches the levels (ignoring the extreme 1%), darkens mid-tones a little so faded
// prints don't wash out, and sharpens so faces survive dithering.
function toneMap(lum) {
  const sorted = Float32Array.from(lum).sort();
  const lo = sorted[Math.floor(sorted.length * 0.01)], hi = sorted[Math.floor(sorted.length * 0.99)];
  const out = new Float32Array(W * H);
  for (let i = 0; i < W * H; i++) {
    const t = Math.min(1, Math.max(0, (lum[i] - lo) / (hi - lo)));
    out[i] = 255 * Math.pow(t, GAMMA);
  }
  // Unsharp mask: add back the difference from a 5x5 box blur
  const blur = new Float32Array(W * H);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    let s = 0, n = 0;
    for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) {
      const xx = x + dx, yy = y + dy;
      if (xx >= 0 && yy >= 0 && xx < W && yy < H) { s += out[yy * W + xx]; n++; }
    }
    blur[y * W + x] = s / n;
  }
  for (let i = 0; i < W * H; i++) out[i] = out[i] + 0.8 * (out[i] - blur[i]);
  return out;
}

// Floyd-Steinberg error diffusion on brightness (serpentine, to avoid streaks).
// Returns 0 = black, 1 = white, 2 = red. Red is only used where the photo is clearly red or
// orange (redness), and counts as a mid-dark tone for the error diffusion.
const RED_TONE = 95;
function dither(tone, redness) {
  const buf = Float32Array.from(tone);
  const out = new Uint8Array(W * H);
  for (let y = 0; y < H; y++) {
    const ltr = y % 2 === 0;
    for (let k = 0; k < W; k++) {
      const x = ltr ? k : W - 1 - k, i = y * W + x, v = buf[i];
      let c, val;
      if (redness && redness[i] > 70 && v > 40 && v < 215 && Math.abs(v - RED_TONE) < 70) {
        c = 2; val = RED_TONE;
      } else if (v < 128) { c = 0; val = 0; } else { c = 1; val = 255; }
      out[i] = c;
      const e = v - val, dir = ltr ? 1 : -1;
      const add = (xx, yy, f) => { if (xx >= 0 && xx < W && yy < H) buf[yy * W + xx] += e * f; };
      add(x + dir, y, 7 / 16);
      add(x - dir, y + 1, 3 / 16);
      add(x, y + 1, 5 / 16);
      add(x + dir, y + 1, 1 / 16);
    }
  }
  return out;
}

async function savePreview(idx, file) {
  const img = new Jimp(W, H);
  const cols = [INK, PAPER, RED_INK];
  for (let i = 0; i < W * H; i++) {
    const c = cols[idx[i]];
    img.bitmap.data.set([c[0], c[1], c[2], 255], i * 4);
  }
  await img.writeAsync(file);
}

// Same bit layout as Canvas3C: black plane 1 = white, red plane 1 = red.
function writeHeader(idx, file, name) {
  const black = new Uint8Array(W * H / 8).fill(0xFF), red = new Uint8Array(W * H / 8);
  for (let i = 0; i < W * H; i++) {
    const byte = i >> 3, bit = 0x80 >> (i & 7);
    if (idx[i] === 0) black[byte] &= ~bit;
    if (idx[i] === 2) red[byte] |= bit;
  }
  const arr = (a) => Array.from(a, (v, i) => (i % 20 === 0 ? '\n  ' : '') + '0x' + v.toString(16).padStart(2, '0')).join(',');
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, `// Generated by tools/make-photo.cjs from ${name}. Do not edit by hand.
#pragma once
#include <Arduino.h>

static const uint8_t PHOTO_BLACK[${black.length}] PROGMEM = {${arr(black)}
};
static const uint8_t PHOTO_RED[${red.length}] PROGMEM = {${arr(red)}
};
`);
}

main().catch(e => { console.error(e); process.exit(1); });
