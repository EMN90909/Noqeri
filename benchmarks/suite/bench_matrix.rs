use std::time::Instant;

const N: usize = 256;

#[inline(never)]
fn bench_matrix() -> i64 {
    let mut a = vec![0i64; N * N];
    let mut b = vec![0i64; N * N];
    let mut c = vec![0i64; N * N];

    for i in 0..N {
        for j in 0..N {
            a[i * N + j] = ((i + j) % 100) as i64;
            b[i * N + j] = ((i * j + 1) % 100) as i64;
            c[i * N + j] = 0;
        }
    }

    for i in 0..N {
        for k in 0..N {
            let r = a[i * N + k];
            for j in 0..N {
                c[i * N + j] += r * b[k * N + j];
            }
        }
    }
    let mut total: i64 = 0;
    for idx in 0..N * N {
        total += c[idx];
    }
    total
}

fn main() {
    let start = Instant::now();
    let res = bench_matrix();
    let elapsed = start.elapsed().as_secs_f64();
    println!("Rust\tmatrix\t{}\t{:.6}", res, elapsed);
}
