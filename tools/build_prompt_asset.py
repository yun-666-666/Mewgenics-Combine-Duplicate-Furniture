"""Build our own in-game prompt; only font/AS3 plumbing comes from MewUI's MIT example."""

from pathlib import Path
import struct
import sys

from swf_frame_scripts import add_stop_classes


class Bits:
    def __init__(self):
        self.bits = []

    def put(self, value, count):
        self.bits.extend((value >> shift) & 1 for shift in range(count - 1, -1, -1))

    def bytes(self):
        self.bits.extend([0] * (-len(self.bits) % 8))
        return bytes(sum(self.bits[i + j] << (7 - j) for j in range(8))
                     for i in range(0, len(self.bits), 8))


def nbits(*values):
    return max(1, *(v.bit_length() + 1 if v >= 0 else (~v).bit_length() + 1
                   for v in values))


def rect(width, height):
    b = Bits()
    count = nbits(width, height)
    b.put(count, 5)
    for value in (0, width, 0, height):
        b.put(value, count)
    return b.bytes()


def matrix(x=0, y=0, scale=1):
    b = Bits()
    b.put(1, 1)
    size = round(scale * 65536)
    count = nbits(size)
    b.put(count, 5)
    b.put(size, count)
    b.put(size, count)
    b.put(0, 1)
    x, y = round(x * 20), round(y * 20)
    count = nbits(x, y)
    b.put(count, 5)
    b.put(x, count)
    b.put(y, count)
    return b.bytes()


def tag(code, body=b''):
    if len(body) < 63:
        return struct.pack('<H', code << 6 | len(body)) + body
    return struct.pack('<HI', code << 6 | 63, len(body)) + body


def tags(data, start=0):
    while start < len(data):
        header, = struct.unpack_from('<H', data, start)
        start += 2
        size = header & 63
        if size == 63:
            size, = struct.unpack_from('<I', data, start)
            start += 4
        end = start + size
        if end > len(data):
            raise ValueError('truncated SWF tag')
        yield header >> 6, data[start:end]
        start = end
        if header >> 6 == 0:
            break


def place(character, depth, name, x=0, y=0, scale=1):
    return tag(26, b'\x26' + struct.pack('<HH', depth, character) +
               matrix(x, y, scale) + name.encode() + b'\0')


def shape(character, width, height, color):
    width, height = width * 20, height * 20
    b = Bits()
    b.put(0, 1)
    b.put(0b00101, 5)  # move + fill1
    b.put(1, 5)
    b.put(0, 1)
    b.put(0, 1)
    b.put(1, 1)
    # SWF edges can encode at most 17 signed bits.
    for delta, vertical in ((width, 0), (height, 1), (-width, 0), (-height, 1)):
        count = max(2, nbits(delta))
        b.put(3, 2)
        b.put(count - 2, 4)
        b.put(0, 1)
        b.put(vertical, 1)
        b.put(delta, count)
    b.put(0, 6)
    return tag(32, struct.pack('<H', character) + rect(width, height) +
               bytes((1, 0, *color, 0, 0x10)) + b.bytes())


def text_definition(template, character, width, height):
    # Retain the example's embedded game font and formatting, but blank the text.
    old_end = 2 + (5 + 4 * (template[2] >> 3) + 7) // 8
    body = bytearray(struct.pack('<H', character) +
                     rect(width, height) + template[old_end:])
    flags_at = 2 + len(rect(width, height))
    body[flags_at] |= 0x60  # WordWrap + Multiline
    if body.count(b'>Test</font>') != 1:
        raise ValueError('MewUI sample text format changed')
    return tag(37, bytes(body).replace(b'>Test</font>', b'></font>'))


def build(source, destination):
    swf = source.read_bytes()
    if swf[:3] != b'FWS':
        raise ValueError('expected uncompressed MewUI example')
    start = 8 + (5 + 4 * (swf[8] >> 3) + 7) // 8 + 4
    source_tags = list(tags(swf, start))
    overlay = next(body for code, body in source_tags
                   if code == 39 and b'test_button\0' in body)
    overlay_id, = struct.unpack_from('<H', overlay)
    text_placement = next(body for code, body in tags(overlay, 4)
                          if code == 26 and b'test_text\0' in body)
    text_id, = struct.unpack_from('<H', text_placement, 3)
    template = next(body for code, body in source_tags
                    if code == 37 and struct.unpack_from('<H', body)[0] == text_id)
    definition_codes = {2, 6, 7, 10, 11, 20, 21, 22, 32, 33, 34, 35, 36,
                        37, 39, 46, 48, 60, 73, 75, 83, 84, 87, 88, 90, 91}
    next_id = max(struct.unpack_from('<H', body)[0]
                  for code, body in source_tags if code in definition_codes) + 1
    dim, border, paper, rule, button, selected, text, prompt, small_edge, small_paper = range(next_id, next_id + 10)
    definitions = (shape(dim, 1280, 720, (12, 10, 8, 170)) +
                   shape(border, 940, 620, (46, 39, 29, 255)) +
                   shape(paper, 930, 610, (242, 231, 206, 255)) +
                   shape(rule, 852, 2, (169, 141, 95, 255)) +
                   shape(button, 180, 44, (212, 196, 163, 255)) +
                   shape(selected, 180, 44, (182, 158, 103, 255)) +
                   text_definition(template, text, 80000, 3000) +
                   shape(small_edge, 820, 260, (46, 39, 29, 255)) +
                   shape(small_paper, 810, 250, (242, 231, 206, 255)))

    # Frame 0 is genuinely empty; frame 1 creates the entire dialog at once.
    panel = bytearray(struct.pack('<HH', prompt, 3) + tag(1))
    panel += place(dim, 1, 'shade')
    panel += place(border, 2, 'edge', 170, 50)
    panel += place(paper, 3, 'paper', 175, 55)
    panel += place(rule, 4, 'rule', 214, 111)
    panel += place(text, 5, 'title', 218, 67, .32)
    for i in range(16):
        panel += place(text, 10 + i, f'line{i}', 218, 130 + i * 26, .22)
    panel += place(text, 30, 'page', 218, 562, .18)
    for i, (name, x) in enumerate((('prev', 218), ('next', 418), ('cancel', 670), ('yes', 870))):
        panel += place(selected if name == 'yes' else button, 40 + i, name, x, 606)
        panel += place(text, 50 + i, name + '_txt', x + 16, 613, .22)
    panel += tag(1)
    # Frame 2 is a compact auto-closing notification, not a full empty dialog.
    for depth in (1, 2, 3, 4, 5, *range(10, 26), 30, *range(40, 44), *range(50, 54)):
        panel += tag(28, struct.pack('<H', depth))
    panel += place(small_edge, 2, 'edge', 230, 220)
    panel += place(small_paper, 3, 'paper', 235, 225)
    panel += place(text, 5, 'title', 266, 239, .28)
    for i in range(4):
        panel += place(text, 10 + i, f'line{i}', 266, 285 + i * 26, .20)
    panel += place(button, 42, 'cancel', 830, 410)
    panel += place(text, 52, 'cancel_txt', 846, 417, .22)
    panel += tag(1) + tag(0)
    definitions += tag(39, panel)

    output = bytearray(swf[:start])
    for code, body in source_tags:
        if code == 82:
            abc_at = body.index(b'\0', 4) + 1
            body = body[:abc_at] + add_stop_classes(body[abc_at:], [b'CdfPrompt'])
        elif code == 76:
            count, = struct.unpack_from('<H', body)
            body = struct.pack('<H', count + 1) + body[2:] + struct.pack('<H', prompt) + b'house_fla.CdfPrompt\0'
        elif code == 39 and struct.unpack_from('<H', body)[0] == overlay_id:
            output += definitions
            # Keep the original root class/binding expected by the game.
            body = struct.pack('<HH', overlay_id, 1) + place(prompt, 1, 'cdf_prompt') + tag(1) + tag(0)
        output += tag(code, body)
    struct.pack_into('<I', output, 4, len(output))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(output)
    # Parse the generated UI and assert the hidden/visible frame contract.
    generated = list(tags(output, start))
    result = next(body for code, body in generated
                  if code == 39 and struct.unpack_from('<H', body)[0] == prompt)
    frames = list(tags(result, 4))
    assert frames[0][0] == 1 and sum(code == 1 for code, _ in frames) == 3
    assert all(f'line{i}\0'.encode() in result for i in range(16))
    assert b'cdf_prompt\0' in output and len(output) == struct.unpack_from('<I', output, 4)[0]
    print(f'Built in-game prompt asset: {destination}')


if __name__ == '__main__':
    build(Path(sys.argv[1]), Path(sys.argv[2]))
