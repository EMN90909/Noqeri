use std::time::Instant;

const N: usize = 2000;

#[inline(never)]
fn bench_sort() -> i64 {
    let mut arr = [0i64; N];
    let mut seed: u64 = 12345;
    for i in 0..N {
        seed = (seed.wrapping_mul(1103515245).wrapping_add(12345)) % 2147483648;
        arr[i] = (seed % 100000) as i64;
    }
    for j in 1..N {
        let key = arr[j];
        let mut k = j as isize - 1;
        while k >= 0 && arr[k as usize] > key {
            arr[(k + 1) as usize] = arr[k as usize];
            k -= 1;
        }
        arr[(k + 1) as usize] = key;
    }
    arr[N - 1]
}

fn main() {
    let start = Instant::now();
    let res = bench_sort();
    let elapsed = start.elapsed().as_secs_f64();
    println!("Rust\tsorting\t{}\t{:.6}", res, elapsed);
}
