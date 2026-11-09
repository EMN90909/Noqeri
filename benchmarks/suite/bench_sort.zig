const std = @import("std");

const N: usize = 2000;
var arr: [N]i64 = undefined;

fn bench_sort() i64 {
    var seed: u64 = 12345;
    for (0..N) |i| {
        seed = (seed *% 1103515245 +% 12345) % 2147483648;
        arr[i] = @intCast(seed % 100000);
    }
    for (1..N) |j| {
        const key = arr[j];
        var k: isize = @as(isize, @intCast(j)) - 1;
        while (k >= 0 and arr[@as(usize, @intCast(k))] > key) : (k -= 1) {
            arr[@as(usize, @intCast(k + 1))] = arr[@as(usize, @intCast(k))];
        }
        arr[@as(usize, @intCast(k + 1))] = key;
    }
    return arr[N - 1];
}

pub fn main() !void {
    var timer = try std.time.Timer.start();
    const res = bench_sort();
    const elapsed = @as(f64, @floatFromInt(timer.read())) / 1e9;
    std.debug.print("Zig\tsorting\t{d}\t{d:.6}\n", .{ res, elapsed });
}
