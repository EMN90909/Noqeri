const { performance } = require('perf_hooks');

const start = performance.now();
const pixels = new Float64Array(512 * 512);
for (let y = 0; y < 512; y++) {
    for (let x = 0; x < 512; x++) {
        const r = (x + y * 3) % 256;
        const g = (x * 7 + y * 13) % 256;
        const b = (x + y) % 256;
        const pixel = (r * 65536) + (g * 256) + b;
        pixels[y * 512 + x] = pixel;
    }
}
let checksum = 0;
for (let i = 0; i < 512 * 512; i++) {
    checksum += pixels[i];
}
const elapsed = (performance.now() - start) / 1000.0;
console.log(`Javascript(Num)\timage\t${checksum}\t${elapsed.toFixed(6)}`);
