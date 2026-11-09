const { performance } = require('perf_hooks');

const start = performance.now();
let checksum = 0n;
for (let y = 0n; y < 512n; y++) {
    for (let x = 0n; x < 512n; x++) {
        const r = (x + y * 3n) % 256n;
        const g = (x * 7n + y * 13n) % 256n;
        const b = (x + y) % 256n;
        const pixel = (r * 65536n) + (g * 256n) + b;
        checksum = checksum + pixel;
    }
}
const elapsed = (performance.now() - start) / 1000.0;
console.log(`JS\timage\t${checksum}\t${elapsed.toFixed(6)}`);
