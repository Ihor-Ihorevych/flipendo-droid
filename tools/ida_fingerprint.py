# Fingerprint every exported function of the DLL open in IDA, so two builds (HP1 and HP2) can be compared.
# Run inside IDA (IDAPython / the IDA MCP): exec(open(r'<repo>/tools/ida_fingerprint.py').read()); fingerprint(out_path)
#
# For each export (decorated C++ name) the incremental-linking thunk ("jmp body") is followed to the real body, then:
#   size, insns  - function size in bytes and instruction count
#   hash         - SHA-1 of the normalized instruction stream: mnemonics, registers, small immediates and struct
#                  displacements kept; code/data addresses replaced by the export/import name they point to, or "A".
#                  Two builds with the same hash have the same code (up to relocation), so a port carries over.
#   shape        - the same with displacements and immediates dropped: equal shape but different hash means only
#                  struct offsets or constants moved (e.g. fields added to AActor), not the logic.
# Output: JSON {decorated name: {ea, body, size, insns, hash, shape}}.

import hashlib
import json

import ida_funcs
import ida_name
import ida_ua
import idautils
import idc


def _body(ea):
    """Follow a jmp thunk to the function it jumps to."""
    for _ in range(4):
        insn = ida_ua.insn_t()
        if ida_ua.decode_insn(insn, ea) and insn.get_canon_mnem() == "jmp" and insn.Op1.type in (ida_ua.o_near, ida_ua.o_far):
            ea = insn.Op1.addr
        else:
            break
    return ea


def _addr_name(ea):
    name = ida_name.get_name(ea) or ""
    if not name or name.startswith(("sub_", "loc_", "unk_", "dword_", "byte_", "word_", "qword_", "off_", "stru_", "asc_", "flt_", "dbl_", "j_")):
        return "A"
    return name


def _normalized(func, shape=False):
    parts = []
    for ea in idautils.FuncItems(func.start_ea):
        insn = ida_ua.insn_t()
        if not ida_ua.decode_insn(insn, ea):
            continue
        ops = []
        for op in insn.ops:
            if op.type == ida_ua.o_void:
                break
            if op.type == ida_ua.o_imm:
                ops.append("i" if shape else ("i%x" % op.value if op.value < 0x10000 else "I"))
            elif op.type in (ida_ua.o_near, ida_ua.o_far):
                target = op.addr
                ops.append("j" if ida_funcs.get_func(target) == func else _addr_name(_body(target)))
            elif op.type == ida_ua.o_mem:
                ops.append("m" + _addr_name(op.addr))
            elif op.type in (ida_ua.o_displ, ida_ua.o_phrase):
                disp = "" if shape else (op.addr if op.addr < 0x10000 else "D")
                ops.append("d%d,%s,%s" % (op.reg, op.specflag1, disp))
            else:
                ops.append("%d:%d" % (op.type, op.reg))
        parts.append(insn.get_canon_mnem() + " " + " ".join(ops))
    return parts


def fingerprint(out_path):
    result = {}
    for _, _, ea, name in idautils.Entries():
        if not name or not name.startswith("?"):
            continue
        body = _body(ea)
        func = ida_funcs.get_func(body)
        if not func:
            continue
        parts = _normalized(func)
        result[name] = {
            "ea": "0x%X" % ea,
            "body": "0x%X" % func.start_ea,
            "size": func.end_ea - func.start_ea,
            "insns": len(parts),
            "hash": hashlib.sha1("\n".join(parts).encode()).hexdigest(),
            "shape": hashlib.sha1("\n".join(_normalized(func, True)).encode()).hexdigest(),
        }
    with open(out_path, "w") as f:
        json.dump(result, f, indent=0, sort_keys=True)
    return len(result)
