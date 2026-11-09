function benchModulo(iterations) {
    let sum = 0n;
    const it = BigInt(iterations);
    for (let i = 1n; i <= it; i++) {
        const m = (i * 37n + 13n) % 997n;
        sum += m;
    }
    return sum;
}

const start = performance.now();
const res = benchModulo(10000000);
const elapsed = (performance.now() - start) / 1000;
console.log(`Javascript\tmodulo\t${res}\t${elapsed.toFixed(6)}`);
