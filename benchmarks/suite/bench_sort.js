const N = 2000;

function benchSort() {
    const arr = new BigInt64Array(N);
    let seed = 12345n;
    for (let i = 0; i < N; i++) {
        seed = (seed * 1103515245n + 12345n) % 2147483648n;
        arr[i] = seed % 100000n;
    }
    for (let j = 1; j < N; j++) {
        const key = arr[j];
        let k = j - 1;
        while (k >= 0 && arr[k] > key) {
            arr[k + 1] = arr[k];
            k--;
        }
        arr[k + 1] = key;
    }
    return arr[N - 1];
}

const start = performance.now();
const res = benchSort();
const elapsed = (performance.now() - start) / 1000;
console.log(`Javascript\tsorting\t${res}\t${elapsed.toFixed(6)}`);
