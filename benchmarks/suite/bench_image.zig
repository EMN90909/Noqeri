const std = @import("std");

pub fn main() !void {
    const start = std.time.nanoTimestamp();
    var checksum: i64 = 0;
    var y: i64 = 0;
    while (y < 512) : (y += 1) {
        var x: i64 = 0;
        while (x < 512) : (x += 1) {
            const r = @rem(x + y * 3, 256);
            const g = @rem(x * 7 + y * 13, 256);
            const b = @rem(x + y, 256);
            const pixel = (r * 65536) + (g * 256) + b;
            checksum += pixel;
        }
    }
    const end = std.time.nanoTimestamp();
    const elapsed = @as(f64, @floatFromInt(end - start)) / 1e9;
    std.debug.print("Zig\timage\t{d}\t{d:.6}\n", .{checksum, elapsed});
}
