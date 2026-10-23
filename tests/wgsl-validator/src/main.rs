use naga::valid::{Capabilities, ValidationFlags, Validator};
use std::env;
use std::fs;

fn main() {
    let files: Vec<_> = env::args().skip(1).collect();
    if files.is_empty() {
        eprintln!("usage: noqeri-wgsl-validator <shader.wgsl>...");
        std::process::exit(2);
    }
    for file in files {
        let source = fs::read_to_string(&file).expect("failed to load generated WGSL");
        let module = naga::front::wgsl::parse_str(&source).unwrap_or_else(|error| {
            eprintln!("{file}: WGSL syntax rejected: {error}");
            std::process::exit(1);
        });
        Validator::new(ValidationFlags::all(), Capabilities::empty())
            .validate(&module)
            .unwrap_or_else(|error| {
                eprintln!("{file}: WGSL semantic validation failed: {error}");
                std::process::exit(1);
            });
        println!("validated Noqeri-generated WGSL: {file}");
    }
}
