use std::time::Instant;

fn main() {
    let start = Instant::now();
    let mut checksum: i64 = 0;
    for y in 0i64..512 {
        for x in 0i64..512 {
            let r = (x + y * 3) % 256;
            let g = (x * 7 + y * 13) % 256;
            let b = (x + y) % 256;
            let pixel = (r * 65536) + (g * 256) + b;
            checksum += pixel;
        }
    }
    let elapsed = start.elapsed().as_secs_f64();
    println!("Rust\timage\t{}\t{:.6}", checksum, elapsed);
}
