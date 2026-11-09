const N = 2000;

function benchSort() {
    const arr = new Int32Array(N);
    let seed = 12345;
    for (let i = 0; i < N; i++) {
        seed = ((Math.imul(seed, 1103515245) + 12345) & 0x7fffffff) % 2147483648;
        arr[i] = seed % 100000;
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
    let checksum = 0;
    for (let i = 0; i < N; i++) {
        if (i < N - 1 && arr[i] > arr[i + 1]) {
            throw new Error(`Sorting verification failed at index ${i}`);
        }
        checksum += arr[i] * (i + 1);
    }
    return checksum;
}

const start = performance.now();
const res = benchSort();
const elapsed = (performance.now() - start) / 1000;
console.log(`Javascript(Num)\tsorting\t${res}\t${elapsed.toFixed(6)}`);
