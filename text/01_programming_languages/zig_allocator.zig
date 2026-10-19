const std = @import("std");

pub const RingBuffer = struct {
    buffer: []u8,
    head: usize = 0,
    tail: usize = 0,
    allocator: std.mem.Allocator,

    pub fn init(allocator: std.mem.Allocator, capacity: usize) !RingBuffer {
        const mem = try allocator.alloc(u8, capacity);
        return RingBuffer{
            .buffer = mem,
            .allocator = allocator,
        };
    }

    pub fn deinit(self: *RingBuffer) void {
        self.allocator.free(self.buffer);
    }

    pub fn writeByte(self: *RingBuffer, byte: u8) bool {
        const next = (self.head + 1) % self.buffer.len;
        if (next == self.tail) return false;
        self.buffer[self.head] = byte;
        self.head = next;
        return true;
    }
};
