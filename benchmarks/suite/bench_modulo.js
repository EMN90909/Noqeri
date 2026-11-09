function benchModulo(iterations) {
    let sum = 0;
    for (let i = 1; i <= iterations; i++) {
        const m = (i * 37 + 13) % 997;
        sum += m;
    }
    return sum;
}

const start = performance.now();
const res = benchModulo(10000000);
const elapsed = (performance.now() - start) / 1000;
console.log(`Javascript(Num)\tmodulo\t${res}\t${elapsed.toFixed(6)}`);
