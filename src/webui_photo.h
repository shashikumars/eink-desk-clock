#pragma once
// The "Upload a photo" page (/photo). Everything heavy happens in the browser: the photo is
// cropped, toned and dithered into the panel's black/white/red, previewed, and only the
// finished 96000-byte image is sent to the clock.
#include <Arduino.h>

static const char PHOTO_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Photo - E-Ink Clock</title>
<style>
body{font-family:system-ui,sans-serif;max-width:560px;margin:0 auto;padding:16px;background:#f4f4f4;color:#222}
h1{font-size:1.4em}.box{background:#fff;padding:16px;border-radius:10px;margin-bottom:16px}
label{display:block;margin:12px 0 4px;font-weight:600}
select,input[type=file]{width:100%;padding:8px;font-size:1em;box-sizing:border-box}
input[type=range]{width:100%}
.row{display:flex;gap:8px}.row button{flex:1}
button{margin-top:12px;padding:12px;width:100%;font-size:1em;border:0;border-radius:8px;background:#222;color:#fff}
button.alt{background:#e2e2e2;color:#222}
button:disabled{opacity:.4}
canvas{width:100%;height:auto;display:block;background:#e4e3dd;border:8px solid #1f1f21;box-sizing:border-box;image-rendering:pixelated}
.msg{padding:10px;border-radius:8px;background:#e6f4ea;margin-top:12px}.err{background:#fce8e6}
small{color:#666}a{color:#222}
</style></head><body>
<h1>Photo</h1>
<div class="box">
  <label for="file">Choose a photo</label>
  <input type="file" id="file" accept="image/*">
  <small>Portrait photos are set up for standing the frame on its side.</small>
</div>
<div class="box" id="edit" hidden>
  <canvas id="out" width="800" height="480"></canvas>
  <button class="alt" id="fit" type="button">Show whole photo</button>
  <label for="orient">Frame position</label>
  <select id="orient">
    <option value="land">Landscape</option>
    <option value="left">Portrait, standing on its left edge (cable at the bottom)</option>
    <option value="right">Portrait, standing on its right edge</option>
  </select>
  <label for="colours">Colours</label>
  <select id="colours"><option value="bwr">Black, white and red</option><option value="bw">Black and white</option></select>
  <label for="pos">Position <small>(which part to keep when cropping)</small></label>
  <input type="range" id="pos" min="0" max="100" value="40">
  <label for="bright">Brightness</label>
  <input type="range" id="bright" min="0" max="100" value="45">
  <label for="detail">Detail</label>
  <input type="range" id="detail" min="0" max="100" value="55">
  <button id="send" type="button">Show on the clock</button>
  <div id="status" hidden></div>
</div>
<p><a href="/">&larr; Back to settings</a></p>
<script>
(() => {
  const $ = id => document.getElementById(id);
  const PAPER = [228, 227, 221], INK = [28, 28, 30], RED = [178, 40, 31];
  let img = null, fill = true, idx = null, W = 800, H = 480;

  $('file').onchange = () => {
    const f = $('file').files[0];
    if (!f) return;
    const i = new Image();
    i.onload = () => { img = i; $('orient').value = i.naturalHeight > i.naturalWidth ? 'left' : 'land'; fill = true;
      $('fit').textContent = 'Show whole photo'; $('edit').hidden = false; render(); };
    i.onerror = () => status('This file could not be opened as a picture. Try a JPEG or PNG.', true);
    i.src = URL.createObjectURL(f);
  };
  $('fit').onclick = () => { fill = !fill; $('fit').textContent = fill ? 'Show whole photo' : 'Fill the screen'; render(); };
  ['orient', 'colours', 'pos', 'bright', 'detail'].forEach(id => $(id).oninput = render);

  // Draws the photo upright (as you'll see it), then dithers it to panel colours.
  function render() {
    if (!img) return;
    const side = $('orient').value !== 'land';
    W = side ? 480 : 800; H = side ? 800 : 480;
    const c = document.createElement('canvas'); c.width = W; c.height = H;
    const g = c.getContext('2d', { willReadFrequently: true });
    g.fillStyle = '#fff'; g.fillRect(0, 0, W, H);
    const iw = img.naturalWidth, ih = img.naturalHeight;
    const s = fill ? Math.max(W / iw, H / ih) : Math.min(W / iw, H / ih);
    const dw = iw * s, dh = ih * s, p = $('pos').value / 100;
    const dx = fill ? (W - dw) * p : (W - dw) / 2, dy = fill ? (H - dh) * p : (H - dh) / 2;
    g.imageSmoothingQuality = 'high';
    g.drawImage(img, dx, dy, dw, dh);

    const d = g.getImageData(0, 0, W, H).data, n = W * H;
    const lum = new Float32Array(n), redness = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      const r = d[i * 4], gg = d[i * 4 + 1], b = d[i * 4 + 2];
      lum[i] = 0.299 * r + 0.587 * gg + 0.114 * b;
      redness[i] = r - Math.max(gg, b);
    }
    idx = dither(tone(lum), $('colours').value === 'bwr' ? redness : null);

    const out = $('out'); out.width = W; out.height = H;
    const o = out.getContext('2d'), im = o.createImageData(W, H);
    const cols = [INK, PAPER, RED];
    for (let i = 0; i < n; i++) { const k = cols[idx[i]]; im.data.set([k[0], k[1], k[2], 255], i * 4); }
    o.putImageData(im, 0, 0);
  }

  // Stretch levels, set mid-tone brightness, sharpen.
  function tone(lum) {
    const n = lum.length, sorted = Float32Array.from(lum).sort();
    const lo = sorted[Math.floor(n * 0.01)], hi = sorted[Math.floor(n * 0.99)] || 255;
    const gamma = 2.2 - 1.6 * ($('bright').value / 100);   // 2.2 (dark) .. 0.6 (light)
    const out = new Float32Array(n);
    for (let i = 0; i < n; i++) out[i] = 255 * Math.pow(Math.min(1, Math.max(0, (lum[i] - lo) / Math.max(1, hi - lo))), gamma);
    const amount = 1.6 * ($('detail').value / 100);
    if (amount > 0) {
      const tmp = new Float32Array(n), blur = new Float32Array(n), R = 2;
      for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        let s = 0, c = 0;
        for (let k = -R; k <= R; k++) { const xx = x + k; if (xx >= 0 && xx < W) { s += out[y * W + xx]; c++; } }
        tmp[y * W + x] = s / c;
      }
      for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        let s = 0, c = 0;
        for (let k = -R; k <= R; k++) { const yy = y + k; if (yy >= 0 && yy < H) { s += tmp[yy * W + x]; c++; } }
        blur[y * W + x] = s / c;
      }
      for (let i = 0; i < n; i++) out[i] += amount * (out[i] - blur[i]);
    }
    return out;
  }

  // Floyd-Steinberg (serpentine). 0 = black, 1 = white, 2 = red (only where clearly red).
  function dither(t, redness) {
    const buf = Float32Array.from(t), out = new Uint8Array(W * H), RED_TONE = 95;
    for (let y = 0; y < H; y++) {
      const ltr = y % 2 === 0, dir = ltr ? 1 : -1;
      for (let k = 0; k < W; k++) {
        const x = ltr ? k : W - 1 - k, i = y * W + x, v = buf[i];
        let c, val;
        if (redness && redness[i] > 70 && v > 40 && v < 215 && Math.abs(v - RED_TONE) < 70) { c = 2; val = RED_TONE; }
        else if (v < 128) { c = 0; val = 0; } else { c = 1; val = 255; }
        out[i] = c;
        const e = v - val;
        if (x + dir >= 0 && x + dir < W) buf[i + dir] += e * 7 / 16;
        if (y + 1 < H) {
          if (x - dir >= 0 && x - dir < W) buf[i + W - dir] += e * 3 / 16;
          buf[i + W] += e * 5 / 16;
          if (x + dir >= 0 && x + dir < W) buf[i + W + dir] += e / 16;
        }
      }
    }
    return out;
  }

  // Upright picture -> the panel's own 800 x 480 orientation, as black and red planes.
  // Standing on its left edge, the panel's right edge is at the top: the picture goes on a
  // quarter turn clockwise. On its right edge, a quarter turn anticlockwise.
  function planes() {
    const black = new Uint8Array(48000).fill(0xFF), red = new Uint8Array(48000), o = $('orient').value;
    for (let py = 0; py < 480; py++) for (let px = 0; px < 800; px++) {
      const u = o === 'land' ? px + py * 800 : o === 'left' ? py + (799 - px) * 480 : (479 - py) + px * 480;
      const c = idx[u], i = py * 800 + px, byte = i >> 3, bit = 0x80 >> (i & 7);
      if (c === 0) black[byte] &= ~bit;
      if (c === 2) red[byte] |= bit;
    }
    const all = new Uint8Array(96000); all.set(black, 0); all.set(red, 48000);
    return all;
  }

  $('send').onclick = async () => {
    if (!idx) return;
    $('send').disabled = true;
    status('Sending to the clock...');
    try {
      const form = new FormData();
      form.append('photo', new Blob([planes()], { type: 'application/octet-stream' }), 'photo.bin');
      const r = await fetch('/photo', { method: 'POST', body: form });
      const t = await r.text();
      if (!r.ok) throw new Error(t || r.statusText);
      status('Sent. The clock is drawing the photo now (about 20 seconds). To show the clock again, change "Show" in settings.');
    } catch (e) {
      status('Could not send the photo: ' + e.message + '. Check you are on the same Wi-Fi and try again.', true);
    }
    $('send').disabled = false;
  };

  function status(msg, err) { const s = $('status'); s.hidden = false; s.className = 'msg' + (err ? ' err' : ''); s.textContent = msg; }
})();
</script></body></html>
)HTML";
