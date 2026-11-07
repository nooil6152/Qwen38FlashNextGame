// peddiff.mjs - ISS-032 audit (c): measure a pedestrian in pixels.
//
// The selftest renders the SAME camera twice - once with a walker pinned 4 m in
// front of it (08_ped_pinned_4m.bmp) and once with that walker put away
// (09_ped_removed.bmp). Everything else (sky, sun, crowd, camera) is identical,
// so the difference between the two files is exactly one body: no colour
// guessing, and no reliance on surfaceAt()/emit(), which is the point.
//
// Rows are reported in SCREEN terms (row 0 = top of the window). A BMP with
// biHeight > 0 stores the image bottom first, so screenRow = h - 1 - fileRow.
//
// Usage: node tools/peddiff.mjs [a_with_ped.bmp] [b_without_ped.bmp]
import fs from 'node:fs';

function readBmp(path) {
    const b = fs.readFileSync(path);
    if (b.toString('latin1', 0, 2) !== 'BM') throw new Error(`${path}: not a BMP`);
    const w = b.readInt32LE(18), h = b.readInt32LE(22);
    if (b.readUInt16LE(28) !== 24) throw new Error(`${path}: need a 24-bit BMP`);
    return { b, w, h, off: b.readInt32LE(10), row: Math.floor((w * 3 + 3) / 4) * 4 };
}

const A = process.argv[2] || 'shots/08_ped_pinned_4m.bmp';
const B = process.argv[3] || 'shots/09_ped_removed.bmp';
const a = readBmp(A), bb = readBmp(B);
if (a.w !== bb.w || a.h !== bb.h) throw new Error('different sizes - not comparable');
const { w, h } = a;
const MID = (h - 1) / 2;                                  // screen row of the horizon (pitch 0)
const THRESH = 24;                                        // sum of |dR|+|dG|+|dB|

let n = 0, minR = 1e9, maxR = -1, minC = 1e9, maxC = -1;
let sumR = 0, sumG = 0, sumB = 0;
const rows = new Map();                                   // screenRow -> changed px
const cols = new Map();
for (let fr = 0; fr < h; ++fr) {                          // file rows, bottom-up
    const sr = h - 1 - fr;                                // screen row
    for (let x = 0; x < w; ++x) {
        const sa = a.off + fr * a.row + 3 * x, sb = bb.off + fr * bb.row + 3 * x;
        const d = Math.abs(a.b[sa] - bb.b[sb]) + Math.abs(a.b[sa + 1] - bb.b[sb + 1])
                + Math.abs(a.b[sa + 2] - bb.b[sb + 2]);
        if (d <= THRESH) continue;
        ++n;
        sumR += a.b[sa + 2]; sumG += a.b[sa + 1]; sumB += a.b[sa];
        if (sr < minR) minR = sr; if (sr > maxR) maxR = sr;
        if (x < minC) minC = x; if (x > maxC) maxC = x;
        rows.set(sr, (rows.get(sr) || 0) + 1);
        cols.set(x, (cols.get(x) || 0) + 1);
    }
}
if (!n) { console.log('NO DIFFERENCE - the pinned walker is not being drawn at all'); process.exit(0); }

// longest run of consecutive rows that changed: the body, ignoring its shadow
let runBest = 0, runStart = 0, runCur = 0, runCurStart = 0;
for (let r = 0; r < h; ++r) {
    if ((rows.get(r) || 0) > 0) { if (!runCur) runCurStart = r; runCur++; if (runCur > runBest) { runBest = runCur; runStart = runCurStart; } }
    else runCur = 0;
}
const top = runStart, bot = runStart + runBest - 1;
const wide = [...cols.entries()].filter(([, c]) => c > 2).map(([x]) => x);

console.log(`${A} vs ${B}  (${w}x${h}, threshold ${THRESH})`);
console.log(`changed pixels: ${n}  bbox rows ${minR}..${maxR}, cols ${minC}..${maxC}`);
console.log(`mean colour of changed px: rgb(${(sumR / n) | 0}, ${(sumG / n) | 0}, ${(sumB / n) | 0})`);
console.log(`main blob: screen rows ${top}..${bot} = ${runBest} px tall, `
    + `cols ${wide[0]}..${wide[wide.length - 1]} = ${wide.length} px wide`);
console.log(`image centre row ${MID.toFixed(0)}: blob top is ${(MID - top).toFixed(0)} px ABOVE centre, `
    + `blob bottom is ${(bot - MID).toFixed(0)} px BELOW centre`);
console.log(`  (a 1.765 m body 4 m away in a ${h} px window at 70 deg vertical fov = `
    + `${(1.765 / (2 * 4 * Math.tan(35 * Math.PI / 180) / h)).toFixed(0)} px)`);
const prof = [];
for (let r = top; r <= bot; r += Math.max(1, Math.floor(runBest / 12)))
    prof.push(`${r}:${rows.get(r) || 0}`);
console.log(`row profile (row:changed px): ${prof.join(' ')}`);
