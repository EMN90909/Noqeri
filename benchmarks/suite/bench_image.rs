use std::time::Instant;

fn main() {
    let start = Instant::now();
    let mut pixels = vec![0i64; 512 * 512];
    for y in 0i64..512 {
        for x in 0i64..512 {
            let r = (x + y * 3) % 256;
            let g = (x * 7 + y * 13) % 256;
            let b = (x + y) % 256;
            let pixel = (r * 65536) + (g * 256) + b;
            pixels[(y * 512 + x) as usize] = pixel;
        }
    }
    let mut checksum: i64 = 0;
    for i in 0..512 * 512 {
        checksum += pixels[i];
    }
    let elapsed = start.elapsed().as_secs_f64();
    println!("Rust\timage\t{}\t{:.6}", checksum, elapsed);
}
