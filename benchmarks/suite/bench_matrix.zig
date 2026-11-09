const std = @import("std");

const N: usize = 256;
var A: [N * N]i64 = undefined;
var B: [N * N]i64 = undefined;
var C: [N * N]i64 = undefined;

fn bench_matrix() i64 {
    for (0..N) |i| {
        for (0..N) |j| {
            A[i * N + j] = @intCast(@mod((i + j), 100));
            B[i * N + j] = @intCast(@mod((i * j + 1), 100));
            C[i * N + j] = 0;
        }
    }
    for (0..N) |i| {
        for (0..N) |k| {
            const r = A[i * N + k];
            for (0..N) |j| {
                C[i * N + j] += r * B[k * N + j];
            }
        }
    }
    var total: i64 = 0;
    for (0..N * N) |idx| {
        total += C[idx];
    }
    return total;
}

pub fn main() !void {
    var timer = try std.time.Timer.start();
    const res = bench_matrix();
    const elapsed = @as(f64, @floatFromInt(timer.read())) / 1e9;
    std.debug.print("Zig\tmatrix\t{d}\t{d:.6}\n", .{ res, elapsed });
}
