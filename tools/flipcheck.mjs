// flipcheck.mjs - ISS-031 orientation probe for Town3D screenshots.
// A 24-bit BMP with biHeight > 0 is bottom-up: file row 0 is the image BOTTOM.
// A correct sky-over-ground shot therefore has dark rows first (road) and
// bright rows last (sky), and the two bands should match the logged gnd/sky.
// Usage: node tools/flipcheck.mjs shots/01_downtown_street.bmp [more.bmp ...]
import fs from 'node:fs';

for (const path of process.argv.slice(2)) {
    const b = fs.readFileSync(path);
    if (b.toString('latin1', 0, 2) !== 'BM') { console.log(`${path}: not a BMP`); continue; }
    const w = b.readInt32LE(18), h = b.readInt32LE(22);
    const bits = b.readUInt16LE(28);
    const off = b.readInt32LE(10);
    // JS "/" is NOT integer division: ((1280*3+3)/4)*4 = 3843, not 3840. Floor it.
    const row = Math.floor((w * 3 + 3) / 4) * 4;
    if (bits !== 24 || h <= 0) {
        console.log(`${path}: unsupported BMP (${bits}-bit, biHeight ${h})`);
        continue;
    }
    const rowLuma = y => {
        let a = 0, n = 0;
        for (let x = 0; x < w; x += 7) {
            const s = off + y * row + 3 * x;                    // stored B,G,R
            if (s + 2 >= b.length) break;
            a += 0.2126 * b[s + 2] + 0.7152 * b[s + 1] + 0.0722 * b[s]; n++;
        }
        return n ? a / n : NaN;
    };
    const band = Math.floor(h / 3);
    let bot = 0, top = 0;
    for (let y = 0; y < band; ++y) bot += rowLuma(y);            // first file rows = image BOTTOM
    for (let y = h - band; y < h; ++y) top += rowLuma(y);        // last file rows  = image TOP
    const bottomBand = bot / band, topBand = top / band;
    const verdict = topBand > bottomBand * 1.5 ? 'OK  (dark ground at the bottom, sky on top)'
        : bottomBand > topBand * 1.5 ? 'MIRRORED (sky sitting at the bottom of the file)'
            : 'ambiguous (not a sky-over-ground shot)';
    console.log(`${path} ${w}x${h} off ${off} row ${row} | bottom band ${bottomBand.toFixed(1)}`
        + ` (want ~gnd), top band ${topBand.toFixed(1)} (want ~sky) | ${verdict}`);
}
