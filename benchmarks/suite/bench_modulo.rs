use std::time::Instant;

#[inline(never)]
fn bench_modulo(iterations: i64) -> i64 {
    let mut sum: i64 = 0;
    for i in 1..=iterations {
        let m = (i * 37 + 13) % 997;
        sum += m;
    }
    sum
}

fn main() {
    let start = Instant::now();
    let res = bench_modulo(10_000_000);
    let elapsed = start.elapsed().as_secs_f64();
    println!("Rust\tmodulo\t{}\t{:.6}", res, elapsed);
}
