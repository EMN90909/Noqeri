const std = @import("std");

var pixels: [512 * 512]i64 = undefined;

pub fn main() !void {
    const start = std.time.nanoTimestamp();
    var y: usize = 0;
    while (y < 512) : (y += 1) {
        var x: usize = 0;
        while (x < 512) : (x += 1) {
            const r: i64 = @intCast((x + y * 3) % 256);
            const g: i64 = @intCast((x * 7 + y * 13) % 256);
            const b: i64 = @intCast((x + y) % 256);
            const pixel = (r * 65536) + (g * 256) + b;
            pixels[y * 512 + x] = pixel;
        }
    }
    var checksum: i64 = 0;
    var i: usize = 0;
    while (i < 512 * 512) : (i += 1) {
        checksum += pixels[i];
    }
    const end = std.time.nanoTimestamp();
    const elapsed = @as(f64, @floatFromInt(end - start)) / 1e9;
    std.debug.print("Zig\timage\t{d}\t{d:.6}\n", .{checksum, elapsed});
}
