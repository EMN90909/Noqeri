const N = 256;

function benchMatrix() {
    const A = new BigInt64Array(N * N);
    const B = new BigInt64Array(N * N);
    const C = new BigInt64Array(N * N);

    for (let i = 0; i < N; i++) {
        for (let j = 0; j < N; j++) {
            A[i * N + j] = BigInt((i + j) % 100);
            B[i * N + j] = BigInt((i * j + 1) % 100);
            C[i * N + j] = 0n;
        }
    }

    for (let i = 0; i < N; i++) {
        for (let k = 0; k < N; k++) {
            const r = A[i * N + k];
            for (let j = 0; j < N; j++) {
                C[i * N + j] += r * B[k * N + j];
            }
        }
    }
    return C[N * N - 1];
}

const start = performance.now();
const res = benchMatrix();
const elapsed = (performance.now() - start) / 1000;
console.log(`Javascript\tmatrix\t${res}\t${elapsed.toFixed(6)}`);
