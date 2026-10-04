# Decompile every non-thunk function of the IDA database that is open into decomp/<Dll>/ next to the database (../ida/hp1/decomp/ for HP1, ../ida/hp2/decomp/ for HP2) (one
# <ADDR>_<name>.c per function, headed with the decorated name and [HP1 0x...] address used by the `// IDA`
# tags) and write decomp/<Dll>_index.tsv. For Engine.dll also write Engine_script_events.json:
# every ENGINE_<Event> FName -> the native functions that raise it (inline eventX wrappers expanded).
# Run inside IDA (e.g. through the IDA MCP): exec(open('tools/ida_dump.py').read()); dump_all('Engine')
# Existing files are kept, so a run that hits `limit` seconds can be repeated to finish.
# The output is derived from EA's binaries: it stays in ../ida/<game>/, never in this repo.
import os, re, time, json, idautils, ida_funcs, idc, ida_hexrays, ida_name

DECOMP = os.path.join(os.path.dirname(os.path.abspath(idc.get_idb_path())), 'decomp')

def _is_thunk(f):
    return bool(f.flags & ida_funcs.FUNC_THUNK) or (f.end_ea - f.start_ea <= 5 and idc.print_insn_mnem(f.start_ea) == 'jmp')

def dump_all(dll, limit=320):
    out = os.path.join(DECOMP, dll)
    os.makedirs(out, exist_ok=True)
    thunk_of = {}
    for ea in idautils.Functions():
        if _is_thunk(ida_funcs.get_func(ea)):
            thunk_of.setdefault(idc.get_operand_value(ea, 0), []).append(idc.get_func_name(ea))
    t0 = time.time(); rows = []; done = failed = 0; finished = True
    for ea in idautils.Functions():
        f = ida_funcs.get_func(ea)
        if _is_thunk(f):
            continue
        n = idc.get_func_name(ea)
        d = ida_name.demangle_name(n, idc.get_inf_attr(idc.INF_SHORT_DN)) or n
        s = re.sub(r'[^A-Za-z0-9_]+', '_', re.sub(r'\(.*', '', d).replace('::', '__')).strip('_')[:80] or 'sub'
        fn = f'{ea:08X}_{s}.c'
        path = os.path.join(out, fn)
        rows.append(f'0x{ea:08X}\t{n}\t{d}\t{f.end_ea - f.start_ea}\t{";".join(thunk_of.get(ea, []))}\t{dll}/{fn}')
        if os.path.exists(path):
            continue
        if time.time() - t0 > limit:
            finished = False
            continue
        try:
            c = str(ida_hexrays.decompile(ea))
        except Exception as e:
            c = f'// decompile failed: {e}\n'
            failed += 1
        dem = ida_name.demangle_name(n, idc.get_inf_attr(idc.INF_LONG_DN)) or ''
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(f'// {dll}.dll {n} [HP1 0x{ea:08X}] size {f.end_ea - f.start_ea}\n// {dem}\n\n{c}\n')
        done += 1
    with open(os.path.join(DECOMP, f'{dll}_index.tsv'), 'w', encoding='utf-8') as fh:
        fh.write('addr\tname\tdemangled\tsize\texported_thunks\tfile\n' + '\n'.join(rows) + '\n')
    if dll == 'Engine':
        dump_script_events(thunk_of)
    return dict(dll=dll, funcs=len(rows), done=done, failed=failed, finished=finished)

def dump_script_events(thunk_of):
    def callers_of(fea):
        res = set()
        for x in idautils.XrefsTo(fea):
            fn = ida_funcs.get_func(x.frm)
            if not fn:
                continue
            if _is_thunk(fn):
                for y in idautils.XrefsTo(fn.start_ea):
                    g = ida_funcs.get_func(y.frm)
                    if g and not _is_thunk(g):
                        res.add(g.start_ea)
            else:
                res.add(fn.start_ea)
        return res
    table = {}
    for ea, name in idautils.Names():
        m = re.match(r'\?ENGINE_(\w+)@@3VFName@@A$', name)
        if not m:
            continue
        real = set()
        for x in idautils.XrefsTo(ea):
            fn = ida_funcs.get_func(x.frm)
            if not fn:
                continue
            n = idc.get_func_name(fn.start_ea)
            if n == '?Init@UEngine@@UAEXXZ_0':  # only registers the names
                continue
            real |= callers_of(fn.start_ea) if n.startswith('?event') else {fn.start_ea}
        table[m.group(1)] = sorted((f'0x{a:08X}', idc.get_func_name(a)) for a in real)
    with open(os.path.join(DECOMP, 'Engine_script_events.json'), 'w') as fh:
        json.dump(table, fh, indent=1)
