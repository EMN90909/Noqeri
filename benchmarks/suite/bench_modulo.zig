const std = @import("std");

fn bench_modulo(iterations: i64) i64 {
    var sum: i64 = 0;
    var i: i64 = 1;
    while (i <= iterations) : (i += 1) {
        const m = @mod((i * 37 + 13), 997);
        sum += m;
    }
    return sum;
}

pub fn main() !void {
    var timer = try std.time.Timer.start();
    const res = bench_modulo(10000000);
    const elapsed = @as(f64, @floatFromInt(timer.read())) / 1e9;
    std.debug.print("Zig\tmodulo\t{d}\t{d:.6}\n", .{ res, elapsed });
}
