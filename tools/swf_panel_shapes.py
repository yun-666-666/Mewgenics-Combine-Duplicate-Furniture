"""SWF DefineShape3 helpers reused from the proven AutoCattery panel path."""

import struct


class Bits:
    def __init__(self):
        self.values = []

    def unsigned(self, value, count):
        for shift in range(count - 1, -1, -1):
            self.values.append((value >> shift) & 1)

    def signed(self, value, count):
        if value < 0:
            value += 1 << count
        self.unsigned(value, count)

    def bytes(self):
        while len(self.values) % 8:
            self.values.append(0)
        output = bytearray(len(self.values) // 8)
        for index, value in enumerate(self.values):
            output[index // 8] |= value << (7 - index % 8)
        return bytes(output)


def _signed_bits(*values):
    return max(1, *(value.bit_length() + 1 if value >= 0
                    else (~value).bit_length() + 1 for value in values))


def _rect(width, height):
    count = _signed_bits(0, width, height)
    bits = Bits()
    bits.unsigned(count, 5)
    for value in (0, width, 0, height):
        bits.signed(value, count)
    return bits.bytes()


def _edge(bits, delta, vertical):
    count = max(2, _signed_bits(delta))
    bits.unsigned(1, 1)
    bits.unsigned(1, 1)
    bits.unsigned(count - 2, 4)
    bits.unsigned(0, 1)
    bits.unsigned(1 if vertical else 0, 1)
    bits.signed(delta, count)


def rectangle_shape(character_id, width_px, height_px, fill, line,
                    line_width_px=3):
    width = width_px * 20
    height = height_px * 20
    output = bytearray(struct.pack('<H', character_id))
    output.extend(_rect(width, height))
    output.extend(bytes((1, 0, *fill)))
    output.extend(bytes((1,)))
    output.extend(struct.pack('<H', line_width_px * 20))
    output.extend(bytes(line))
    output.append(0x11)

    shape = Bits()
    shape.unsigned(0, 1)
    shape.unsigned(0b01101, 5)
    shape.unsigned(1, 5)
    shape.signed(0, 1)
    shape.signed(0, 1)
    shape.unsigned(1, 1)
    shape.unsigned(1, 1)
    _edge(shape, width, False)
    _edge(shape, height, True)
    _edge(shape, -width, False)
    _edge(shape, -height, True)
    shape.unsigned(0, 6)
    output.extend(shape.bytes())
    return bytes(output)


def _tag(code, body=b''):
    if len(body) < 63:
        return struct.pack('<H', code << 6 | len(body)) + body
    return struct.pack('<HI', code << 6 | 63, len(body)) + body


def _identity_matrix():
    bits = Bits()
    bits.unsigned(0, 1)
    bits.unsigned(0, 1)
    bits.unsigned(1, 5)
    bits.signed(0, 1)
    bits.signed(0, 1)
    return bits.bytes()


def _place(character_id):
    return bytes((0x06,)) + struct.pack('<HH', 1, character_id) + _identity_matrix()


def three_frame_sprite(character_id, normal_shape_id, pressed_shape_id):
    output = bytearray(struct.pack('<HH', character_id, 3))
    output.extend(_tag(1))
    output.extend(_tag(26, _place(normal_shape_id)))
    output.extend(_tag(1))
    output.extend(_tag(26, _place(pressed_shape_id)))
    output.extend(_tag(1))
    output.extend(_tag(0))
    return bytes(output)
