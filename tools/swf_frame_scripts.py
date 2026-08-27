"""Reuse the pinned House SWF's AS3 first-frame stop script for private clips.

The game's DefineSprite compiler skips AVM1 DoAction tags. Keep the existing
DoABC block and clone its stop-only class instead of adding another VM/block.
"""

from __future__ import annotations

def u30(value: int) -> bytes:
    result = bytearray()
    while value >= 0x80:
        result.append((value & 0x7F) | 0x80)
        value >>= 7
    result.append(value)
    return bytes(result)


class Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0

    def take(self, count: int) -> bytes:
        result = self.data[self.pos:self.pos + count]
        if len(result) != count:
            raise ValueError("truncated ABC data")
        self.pos += count
        return result

    def byte(self) -> int:
        return self.take(1)[0]

    def uint(self) -> int:
        value = 0
        for shift in range(0, 35, 7):
            byte = self.byte()
            value |= (byte & 0x7F) << shift
            if byte < 0x80:
                return value
        raise ValueError("invalid ABC integer")

    def traits(self) -> list[tuple[int, int, int, int]]:
        result = []
        for _ in range(self.uint()):
            name = self.uint()
            flags = self.byte()
            kind = flags & 0x0F
            slot = self.uint()
            target = self.uint()
            if kind in (0, 6):
                if self.uint():
                    self.byte()
            elif kind not in (1, 2, 3, 4, 5):
                raise ValueError(f"unsupported ABC trait {kind}")
            if flags & 0x40:
                for _ in range(self.uint()):
                    self.uint()
            result.append((name, kind, slot, target))
        return result


class Abc:
    """Read section boundaries so existing bytecode can stay unchanged."""

    def __init__(self, data: bytes) -> None:
        r = Reader(data)
        self.header = r.take(4)
        self.sections: dict[str, tuple[int, bytes]] = {}

        def section(name, read_entry, implicit_zero=False):
            count = r.uint()
            start = r.pos
            entries = [read_entry() for _ in range(
                max(0, count - int(implicit_zero)))]
            self.sections[name] = (count, data[start:r.pos])
            return ([None] + entries) if implicit_zero else entries

        section("ints", r.uint, True)
        section("uints", r.uint, True)
        section("doubles", lambda: r.take(8), True)
        self.strings = section("strings", lambda: r.take(r.uint()), True)
        self.namespaces = section(
            "namespaces", lambda: (r.byte(), r.uint()), True)
        section("nssets", lambda: [r.uint() for _ in range(r.uint())], True)

        def multiname():
            kind = r.byte()
            if kind in (7, 13, 9, 14):
                return kind, r.uint(), r.uint()
            if kind in (15, 16, 27, 28):
                return kind, r.uint()
            if kind in (17, 18):
                return (kind,)
            if kind == 29:
                return kind, r.uint(), [r.uint() for _ in range(r.uint())]
            raise ValueError(f"unsupported ABC multiname {kind}")

        self.multinames = section("multinames", multiname, True)

        def method():
            params = r.uint()
            r.uint()
            for _ in range(params):
                r.uint()
            r.uint()
            flags = r.byte()
            if flags & 8:
                for _ in range(r.uint()):
                    r.uint()
                    r.byte()
            if flags & 0x80:
                for _ in range(params):
                    r.uint()

        section("methods", method)

        def metadata():
            r.uint()
            for _ in range(r.uint() * 2):
                r.uint()

        section("metadata", metadata)
        count = r.uint()
        self.instances = []
        start = r.pos
        for _ in range(count):
            item_start = r.pos
            name = r.uint()
            after_name = r.pos
            superclass = r.uint()
            flags = r.byte()
            if flags & 8:
                r.uint()
            for _ in range(r.uint()):
                r.uint()
            initializer = r.uint()
            traits = r.traits()
            self.instances.append((name, superclass, initializer, traits,
                                   data[after_name:r.pos],
                                   data[item_start:r.pos]))
        self.sections["instances"] = count, data[start:r.pos]
        self.classes = []
        start = r.pos
        for _ in range(count):
            item_start = r.pos
            r.uint()
            r.traits()
            self.classes.append(data[item_start:r.pos])
        self.class_bytes = data[start:r.pos]

        def script():
            initializer = r.uint()
            return initializer, r.traits()

        self.scripts = section("scripts", script)
        self.bodies = {}

        def body():
            method_id = r.uint()
            limits = [r.uint() for _ in range(4)]
            code = r.take(r.uint())
            tail_start = r.pos
            for _ in range(r.uint()):
                for _ in range(5):
                    r.uint()
            r.traits()
            self.bodies[method_id] = limits, code, data[tail_start:r.pos]

        section("bodies", body)
        if r.pos != len(data):
            raise ValueError("unexpected data after ABC body table")

    def name(self, index: int) -> bytes:
        kind, _, name = self.multinames[index]
        if kind not in (7, 13):
            raise ValueError("expected a qualified class/method name")
        return self.strings[name]


def add_stop_classes(abc_data: bytes, class_names: list[bytes]) -> bytes:
    abc = Abc(abc_data)
    template_index = next(i for i, item in enumerate(abc.instances)
                          if abc.name(item[0]) == b"coinpulse_117")
    name, _, initializer, traits, instance_tail, _ = abc.instances[template_index]
    if len(traits) != 1 or abc.name(traits[0][0]) != b"frame1":
        raise ValueError("source first-frame stop class changed")
    stop_code = Reader(abc.bodies[traits[0][3]][1])
    if stop_code.take(3) != b"\xd0\x30\x5d":
        raise ValueError("source frame script is not stop-only")
    stop_name = stop_code.uint()
    stop_multiname = abc.multinames[stop_name]
    if (stop_multiname[0] != 9 or
            abc.strings[stop_multiname[1]] != b"stop" or
            stop_code.take(1) != b"\x4f" or stop_code.uint() != stop_name or
            stop_code.take(2) != b"\x00\x47" or
            stop_code.pos != len(stop_code.data)):
        raise ValueError("source frame script is not stop-only")
    # The original constructor registers frame1 on zero-based frame 0.
    if b"\x24\x00\xd0\x66" not in abc.bodies[initializer][1]:
        raise ValueError("source constructor does not stop on frame 0")
    script_init = next(init for init, script_traits in abc.scripts
                       if script_traits == [(name, 4, 0, template_index)])
    limits, script_code, script_tail = abc.bodies[script_init]
    namespace = abc.multinames[name][1]
    additions: dict[str, bytes] = {}

    def add(section: str, value: bytes) -> None:
        additions[section] = additions.get(section, b"") + value

    for index, class_name in enumerate(class_names):
        if class_name in abc.strings:
            raise ValueError("private stop class already exists")
        string_id = abc.sections["strings"][0] + index
        qname = abc.sections["multinames"][0] + index
        class_id = len(abc.instances) + index
        method_id = abc.sections["methods"][0] + index
        add("strings", u30(len(class_name)) + class_name)
        add("multinames", b"\x07" + u30(namespace) + u30(string_id))
        add("instances", u30(qname) + instance_tail)
        add("classes", abc.classes[template_index])
        add("methods", b"\x00\x00\x00\x00")  # () -> *, unnamed, no flags
        add("scripts", u30(method_id) + b"\x01" + u30(qname) +
            b"\x04\x00" + u30(class_id))

        # Clone the source's class-registration script, keeping its scope chain.
        reader = Reader(script_code)
        code = bytearray()
        replacements = []
        while reader.pos < len(script_code):
            op = reader.byte()
            code.append(op)
            if op in (0x5D, 0x60, 0x58, 0x68):
                operand = reader.uint()
                if op in (0x5D, 0x68):
                    operand = qname
                    replacements.append(op)
                elif op == 0x58:
                    if operand != template_index:
                        raise ValueError("unexpected source class registration")
                    operand = class_id
                    replacements.append(op)
                code.extend(u30(operand))
            elif op not in (0xD0, 0x30, 0x1D, 0x47):
                raise ValueError(f"unexpected source registration opcode {op}")
        if replacements != [0x5D, 0x58, 0x68]:
            raise ValueError("incomplete source class registration")
        add("bodies", u30(method_id) + b"".join(map(u30, limits)) +
            u30(len(code)) + code + script_tail)

    result = bytearray(abc.header)
    for section, (count, content) in abc.sections.items():
        extra = additions.get(section, b"")
        result.extend(u30(count + (len(class_names) if extra else 0)))
        result.extend(content)
        result.extend(extra)
        if section == "instances":
            result.extend(abc.class_bytes)
            result.extend(additions.get("classes", b""))
    Abc(bytes(result))
    return bytes(result)
