const { performance } = require('perf_hooks');

const start = performance.now();
const pixels = new BigInt64Array(512 * 512);
for (let y = 0n; y < 512n; y++) {
    for (let x = 0n; x < 512n; x++) {
        const r = (x + y * 3n) % 256n;
        const g = (x * 7n + y * 13n) % 256n;
        const b = (x + y) % 256n;
        const pixel = (r * 65536n) + (g * 256n) + b;
        pixels[Number(y * 512n + x)] = pixel;
    }
}
let checksum = 0n;
for (let i = 0; i < 512 * 512; i++) {
    checksum += pixels[i];
}
const elapsed = (performance.now() - start) / 1000.0;
console.log(`Javascript(BigInt)\timage\t${checksum}\t${elapsed.toFixed(6)}`);
