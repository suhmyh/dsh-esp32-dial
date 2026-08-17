// Rasterise the DeepSeek whale SVG path into an LVGL ALPHA_8BIT C array.
//
// Why hand-rolled: the toolchain has no sharp/canvas (npm install is blocked
// by cache permissions), and the icon is a single SVG <path> with only M/L/C/Z
// commands. Flattening cubics to line segments and running a scanline
// even-odd fill is ~80 lines and needs no dependencies.
//
// Supersampling: we rasterise at SS× the target size and box-average down, so
// the 16px icon keeps the whale's thin tail and eye readable instead of
// aliasing them away.

const fs = require("node:fs");
const path = require("node:path");

const SVG_PATH = process.argv[2];
const OUT_C = process.argv[3];
const SIZE = Number(process.argv[4] ?? 28);
const SS = 8; // supersample factor

const svg = fs.readFileSync(SVG_PATH, "utf8");

// viewBox → source coordinate space
const vb = /viewBox="([\d.\s-]+)"/.exec(svg);
const [vbX, vbY, vbW, vbH] = vb ? vb[1].trim().split(/\s+/).map(Number) : [0, 0, 50, 50];

// the single <path d="...">
const dm = /\sd="([^"]+)"/.exec(svg);
if (!dm) throw new Error("no path data found");
const d = dm[1];

// ── tokenise path data ───────────────────────────────────────────────────
const tokens = d.match(/[MmLlCcZzHhVvSsQqTtAa]|-?\d*\.?\d+(?:e[-+]?\d+)?/gi) ?? [];

// ── flatten to polygons (arrays of [x,y]) ────────────────────────────────
const polys = [];
let poly = null;
let cx = 0, cy = 0;      // current point
let sx = 0, sy = 0;      // subpath start
let px = 0, py = 0;      // previous control point (for S/T)
let cmd = "";
let i = 0;

const num = () => Number(tokens[i++]);
const push = (x, y) => { poly.push([x, y]); };

// cubic Bézier → line segments
function cubic(x1, y1, x2, y2, x3, y3) {
	const steps = 24;
	for (let s = 1; s <= steps; s++) {
		const t = s / steps, u = 1 - t;
		const a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, e = t * t * t;
		push(a * cx + b * x1 + c * x2 + e * x3, a * cy + b * y1 + c * y2 + e * y3);
	}
	px = x2; py = y2;
	cx = x3; cy = y3;
}

while (i < tokens.length) {
	const tk = tokens[i];
	if (/^[MmLlCcZzHhVvSs]$/.test(tk)) { cmd = tk; i++; }
	switch (cmd) {
		case "M": case "m": {
			let x = num(), y = num();
			if (cmd === "m") { x += cx; y += cy; }
			if (poly && poly.length > 2) polys.push(poly);
			poly = [];
			cx = sx = x; cy = sy = y; px = cx; py = cy;
			push(cx, cy);
			cmd = cmd === "M" ? "L" : "l";
			break;
		}
		case "L": case "l": {
			let x = num(), y = num();
			if (cmd === "l") { x += cx; y += cy; }
			cx = x; cy = y; px = cx; py = cy;
			push(cx, cy);
			break;
		}
		case "H": case "h": {
			let x = num();
			if (cmd === "h") x += cx;
			cx = x; px = cx; py = cy; push(cx, cy);
			break;
		}
		case "V": case "v": {
			let y = num();
			if (cmd === "v") y += cy;
			cy = y; px = cx; py = cy; push(cx, cy);
			break;
		}
		case "C": case "c": {
			let x1 = num(), y1 = num(), x2 = num(), y2 = num(), x3 = num(), y3 = num();
			if (cmd === "c") { x1 += cx; y1 += cy; x2 += cx; y2 += cy; x3 += cx; y3 += cy; }
			cubic(x1, y1, x2, y2, x3, y3);
			break;
		}
		case "S": case "s": {
			let x2 = num(), y2 = num(), x3 = num(), y3 = num();
			if (cmd === "s") { x2 += cx; y2 += cy; x3 += cx; y3 += cy; }
			// reflect previous control point
			const x1 = 2 * cx - px, y1 = 2 * cy - py;
			cubic(x1, y1, x2, y2, x3, y3);
			break;
		}
		case "Z": case "z": {
			if (poly && poly.length > 2) polys.push(poly);
			poly = [];
			cx = sx; cy = sy; px = cx; py = cy;
			push(cx, cy);
			i++;
			cmd = "";
			break;
		}
		default:
			i++; // skip anything unsupported rather than looping forever
	}
}
if (poly && poly.length > 2) polys.push(poly);

// ── scanline fill at supersampled resolution ─────────────────────────────
const N = SIZE * SS;
const scale = Math.min(N / vbW, N / vbH);
const offX = (N - vbW * scale) / 2 - vbX * scale;
const offY = (N - vbH * scale) / 2 - vbY * scale;

// device-space edges
const edges = [];
for (const p of polys) {
	for (let k = 0; k < p.length; k++) {
		const [x0, y0] = p[k];
		const [x1, y1] = p[(k + 1) % p.length];
		const ax = x0 * scale + offX, ay = y0 * scale + offY;
		const bx = x1 * scale + offX, by = y1 * scale + offY;
		if (ay !== by) edges.push([ax, ay, bx, by]);
	}
}

const hi = new Uint8Array(N * N);
const xs = [];
for (let y = 0; y < N; y++) {
	const yc = y + 0.5;
	xs.length = 0;
	for (const [ax, ay, bx, by] of edges) {
		if ((yc >= ay && yc < by) || (yc >= by && yc < ay)) {
			xs.push(ax + ((yc - ay) / (by - ay)) * (bx - ax));
		}
	}
	if (xs.length < 2) continue;
	xs.sort((a, b) => a - b);
	// even-odd: fill between alternating crossings
	for (let k = 0; k + 1 < xs.length; k += 2) {
		const from = Math.max(0, Math.ceil(xs[k] - 0.5));
		const to = Math.min(N - 1, Math.floor(xs[k + 1] - 0.5));
		for (let x = from; x <= to; x++) hi[y * N + x] = 255;
	}
}

// ── box-downsample to SIZE×SIZE alpha ────────────────────────────────────
const alpha = new Uint8Array(SIZE * SIZE);
for (let y = 0; y < SIZE; y++) {
	for (let x = 0; x < SIZE; x++) {
		let sum = 0;
		for (let dy = 0; dy < SS; dy++) {
			const row = (y * SS + dy) * N + x * SS;
			for (let dx = 0; dx < SS; dx++) sum += hi[row + dx];
		}
		alpha[y * SIZE + x] = Math.round(sum / (SS * SS));
	}
}

// ── emit the C file as TRUE_COLOR_ALPHA (RGB565 + per-pixel alpha) ────────
//
// Why TRUE_COLOR and not ALPHA_8BIT: the recolor mechanism for ALPHA masks
// was unreliable on the ESP32-S3 LVGL 8.4 build (icon rendered invisible).
// TRUE_COLOR_ALPHA bakes the colour (DeepSeek blue) into every pixel so no
// recolor style is needed; the widget just displays it.
//
// Pixel layout: for each (x,y) we emit two bytes of RGB565 followed by one
// byte of alpha. LV_COLOR_16_SWAP is irrelevant here because the draw layer
// copies through lv_color_t, which handles its own byte order.
const R = 0x1E, G = 0x88, B = 0xE5; // DeepSeek blue
// RGB565: rrrr r0gg gggb bbbb
const rgb = ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3);
const bHi = (rgb >> 8) & 0xFF;
const bLo = rgb & 0xFF;

// Byte order: this firmware builds LVGL with LV_COLOR_16_SWAP=1, so an
// lv_color_t in memory is {low byte, high byte}. TRUE_COLOR_ALPHA data is
// read straight into lv_color_t, so we must emit low-then-high to match —
// emitting high-first tints the whale an unrelated colour.
const cells = [];
for (let y = 0; y < SIZE; y++) {
	for (let x = 0; x < SIZE; x++) {
		const a = alpha[y * SIZE + x];
		cells.push("0x" + bLo.toString(16).padStart(2, "0"));
		cells.push("0x" + bHi.toString(16).padStart(2, "0"));
		cells.push("0x" + a.toString(16).padStart(2, "0"));
	}
}
// 16 bytes per row, wrap every 6 pixels (18 bytes)
const rows = [];
for (let i = 0; i < cells.length; i += 36) {
	rows.push("    " + cells.slice(i, i + 36).join(","));
}

const out = `#include "WhaleIcon.h"

// DeepSeek whale — ${SIZE}×${SIZE} LV_IMG_CF_TRUE_COLOR_ALPHA.
//
// GENERATED FILE. Do not edit by hand.
//   node tools/svg-to-lvgl-alpha.js <favicon.svg> <this file> ${SIZE}
// Source: the DSH web UI's own /favicon.svg (the official DeepSeek whale),
// rasterised at ${SS}× and box-averaged down, so the tail and eye survive at
// this size.
//
// Each pixel is two bytes RGB565 (DeepSeek blue) + one byte alpha, so the
// colour is baked in and no img_recolor style is needed. This avoids the
// ALPHA_8BIT recolor path that rendered invisibly on LVGL 8.4.
static const uint8_t whale_pixels[${SIZE * SIZE * 3}] = {
${rows.join(",\n")},
};

const lv_img_dsc_t whaleIcon = {
    .header = {
        .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
        .always_zero = 0,
        .reserved = 0,
        .w = ${SIZE},
        .h = ${SIZE},
    },
    .data_size = sizeof(whale_pixels),
    .data = whale_pixels,
};
`;

fs.writeFileSync(OUT_C, out, "utf8");

// coverage report, so a silently-empty mask cannot slip through
let lit = 0;
for (const v of alpha) if (v > 8) lit++;
console.log(`${OUT_C}: ${SIZE}x${SIZE}, ${lit}/${SIZE * SIZE} pixels lit (${Math.round((lit / (SIZE * SIZE)) * 100)}%)`);
