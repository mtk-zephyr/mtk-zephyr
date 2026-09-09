#!/usr/bin/env python3
"""Verify Jailhouse cell binaries — parse the memory regions and check the
inmate window against what Zephyr's devicetree declares.

Parses the real struct layout from include/jailhouse/cell-config.h rather than
scanning for byte patterns, so a malformed or unexpectedly-shaped file is
reported as such instead of silently matching nothing.

Usage:
    verify-jailhouse-cells.py [--expect-size 0x800000] [--base 0x6b000000] FILE...

Exit status is non-zero if any file fails a check, so it can gate a deployment.
"""
import argparse
import struct
import sys

CELL_SIG = b'JHCLL'
SYS_SIG = b'JHSYS'
CONFIG_REVISION = 14

# struct jailhouse_cell_desc, packed. See include/jailhouse/cell-config.h.
#   char signature[5]; u8 architecture; u16 revision;
#   char name[32]; u32 id; u32 flags;
#   u32 cpu_set_size, smc_ids_size, num_memory_regions, num_cache_regions,
#       num_irqchips, num_pio_regions, num_pci_devices, num_pci_caps,
#       num_stream_ids, num_vendors, vpci_irq_base;
#   u64 cpu_reset_address, msg_reply_timeout;
#   struct jailhouse_console console;   /* 32 bytes */
CELL_DESC_FMT = '<5sBH32sII11I2Q32s'
CELL_DESC_SIZE = struct.calcsize(CELL_DESC_FMT)
assert CELL_DESC_SIZE == 140, CELL_DESC_SIZE

# Offset of root_cell within struct jailhouse_system (see check() for the
# derivation). Cross-checked against genio-700-evk.cell, whose name field
# lands at 368 = 360 + 8.
SYS_ROOT_CELL_OFFSET = 360

MEM_FMT = '<QQQQ'
MEM_SIZE = struct.calcsize(MEM_FMT)

FLAGS = [
    (0x0001, 'READ'), (0x0002, 'WRITE'), (0x0004, 'EXECUTE'),
    (0x0008, 'DMA'), (0x0010, 'IO'), (0x0020, 'COMM_REGION'),
    (0x0040, 'LOADABLE'), (0x0080, 'ROOTSHARED'),
    (0x0100, 'NO_HUGEPAGES'), (0x8000, 'IO_UNALIGNED'),
    # IO access widths occupy bits 16..19 (JAILHOUSE_MEM_IO_WIDTH_SHIFT).
    (1 << 16, 'IO_8'), (2 << 16, 'IO_16'), (4 << 16, 'IO_32'), (8 << 16, 'IO_64'),
]
LOADABLE = 0x0040


def decode_flags(f):
    names = [n for bit, n in FLAGS if f & bit]
    left = f & ~sum(bit for bit, _ in FLAGS)
    if left:
        names.append(f'0x{left:x}')
    return '|'.join(names) or '0'


def human(n):
    if n >= 1 << 20 and n % (1 << 20) == 0:
        return f'{n >> 20} MB'
    if n >= 1 << 10 and n % (1 << 10) == 0:
        return f'{n >> 10} KB'
    return f'{n} B'


def parse_cell(data, off):
    """Parse a jailhouse_cell_desc at 'off'; return (info, regions)."""
    fields = struct.unpack_from(CELL_DESC_FMT, data, off)
    sig, arch, rev, name = fields[0], fields[1], fields[2], fields[3]
    # fields[4] = id, fields[5] = flags; the 11 counts start at index 6.
    cpu_set_size, smc_ids_size, num_mem = fields[6], fields[7], fields[8]

    info = {
        'signature': sig,
        'revision': rev,
        'name': name.split(b'\0')[0].decode('ascii', 'replace'),
        'num_memory_regions': num_mem,
    }

    # mem_regions = cell_desc + cpu_set_size + smc_ids_size * sizeof(u32).
    # Note smc_ids_size is a COUNT, not bytes — see jailhouse_cell_smc_ids().
    mem_off = off + CELL_DESC_SIZE + cpu_set_size + smc_ids_size * 4
    regions = []
    for i in range(num_mem):
        o = mem_off + i * MEM_SIZE
        if o + MEM_SIZE > len(data):
            raise ValueError(f'memory region {i} runs past end of file')
        phys, virt, size, flags = struct.unpack_from(MEM_FMT, data, o)
        regions.append({'phys': phys, 'virt': virt, 'size': size, 'flags': flags})
    return info, regions


def check(path, expect_size, base, verbose, cell_name='zephyr'):
    data = open(path, 'rb').read()
    problems = []
    sys_revision = None

    if data[:5] == CELL_SIG:
        off = 0
        kind = 'inmate cell'
    elif data[:5] == SYS_SIG:
        # Root cell config. The embedded root_cell descriptor carries no
        # signature of its own (the config sources never set one), so it has to
        # be located by offset. struct jailhouse_system, all packed:
        #   signature[5] arch(1) revision(2) flags(4)              =  12
        #   hypervisor_memory  (struct jailhouse_memory)           =  32 ->  44
        #   debug_console      (struct jailhouse_console)          =  32 ->  76
        #   platform_info:
        #     pci_mmconfig_base(8) end_bus(1) is_virtual(1) dom(2) =  12
        #     iommu_units[8] * sizeof(jailhouse_iommu)=28          = 224 -> 236
        #     union { x86 = 16, arm = 48 } -> 48                   ->     284
        #   root_cell                                              -> 76 + 284
        off = SYS_ROOT_CELL_OFFSET
        kind = 'root cell (JHSYS)'
        # The revision lives in the JHSYS header; the embedded root_cell
        # descriptor leaves its own signature and revision zeroed.
        sys_revision = struct.unpack_from('<H', data, 6)[0]
    else:
        return [f'{path}: unrecognised signature {data[:5]!r} '
                f'(expected {CELL_SIG!r} or {SYS_SIG!r})']

    try:
        info, regions = parse_cell(data, off)
    except (struct.error, ValueError) as e:
        return [f'{path}: parse failed at offset {off}: {e}']

    # The root-cell offset is computed, not discovered, so sanity-check it
    # rather than reporting nonsense from a wrong struct layout.
    if not info['name'].isprintable() or not 0 < info['num_memory_regions'] < 256:
        return [f'{path}: implausible descriptor at offset {off} '
                f'(name={info["name"]!r}, {info["num_memory_regions"]} regions) — '
                f'struct layout may have changed']

    print(f'=== {path}')
    print(f'    {kind}, name={info["name"]!r}, '
          f'revision={sys_revision if kind.startswith("root") else info["revision"]}, '
          f'{info["num_memory_regions"]} memory region(s)')
    revision = sys_revision if kind.startswith('root') else info['revision']
    if revision != CONFIG_REVISION:
        problems.append(f'{path}: config revision {revision} '
                        f'!= expected {CONFIG_REVISION}')

    inmate = []
    for r in regions:
        marker = ''
        if r['phys'] == base and r['flags'] & LOADABLE:
            inmate.append(r)
            marker = '   <-- inmate window'
        if verbose or marker:
            print(f'      phys 0x{r["phys"]:<10x} virt 0x{r["virt"]:<10x} '
                  f'size 0x{r["size"]:<9x} ({human(r["size"]):>6})  '
                  f'{decode_flags(r["flags"])}{marker}')

    if kind.startswith('root'):
        # Root cells declare the reservation without LOADABLE; report it.
        res = [r for r in regions if r['phys'] == base]
        if res:
            print(f'      reserves {human(res[0]["size"])} at 0x{base:x} '
                  f'(inmates are carved from this)')
            if res[0]['size'] < expect_size:
                problems.append(
                    f'{path}: root cell reserves only {human(res[0]["size"])} '
                    f'at 0x{base:x}, less than the {human(expect_size)} an '
                    f'inmate is expected to claim')
        else:
            print(f'      no region at 0x{base:x}')
        print()
        return problems

    if not inmate:
        problems.append(f'{path}: no LOADABLE region at phys 0x{base:x}')
    elif len(inmate) > 1:
        problems.append(f'{path}: {len(inmate)} LOADABLE regions at 0x{base:x}, expected 1')
    elif info['name'] != cell_name:
        print(f'      note: cell is named {info["name"]!r}, not {cell_name!r} — '
              f'size not enforced ({human(inmate[0]["size"])})')
    else:
        got = inmate[0]['size']
        if got != expect_size:
            problems.append(
                f'{path}: inmate window is {human(got)} (0x{got:x}), '
                f'expected {human(expect_size)} (0x{expect_size:x})')
        else:
            print(f'      OK: inmate window is {human(got)}')
    print()
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='+')
    ap.add_argument('--expect-size', default='0x800000',
                    help='required inmate window size (default 0x800000 = 8 MB)')
    ap.add_argument('--cell-name', default='zephyr',
                    help='only enforce the size on inmate cells with this name '
                         '(default "zephyr"); others are reported, not judged')
    ap.add_argument('--base', default='0x6b000000',
                    help='inmate window physical base (default 0x6b000000)')
    ap.add_argument('-v', '--verbose', action='store_true',
                    help='list every memory region, not just the inmate window')
    args = ap.parse_args()

    expect = int(args.expect_size, 0)
    base = int(args.base, 0)

    problems = []
    for f in args.files:
        try:
            problems += check(f, expect, base, args.verbose, args.cell_name)
        except OSError as e:
            problems.append(f'{f}: {e}')

    if problems:
        print('FAILED:')
        for p in problems:
            print(f'  {p}')
        return 1
    print(f'All {len(args.files)} file(s) OK: inmate window {human(expect)} '
          f'at 0x{base:x}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
