"""MIG-0010 update: generates test.out/test.epsg - no real .out/.epsg
test files exist anywhere in this project's corpus (FT.OUT/FT.EPSG are
obscure raw-register-trace formats with no known real-world samples),
so these two synthetic files (a modest, non-trivial sequence of AY
register writes exercising both formats' own record types, wraparound/
sentinel handling, and multiple output ticks) are what run_diff.sh's
own out_file/epsg_file/out_psg_export/epsg_psg_export gates validate
against the real Pascal oracle (OracleHarness.pas's RunOUTFileTest/
RunEPSGFileTest/RunOUTPSGExportTest/RunEPSGPSGExportTest). Re-run this
script (`python3 gen_out_epsg.py`) only if you intend to regenerate
these fixtures - the checked-in test.out/test.epsg are the actual
oracle-diff-validated byte content, not just a same-script-produces-
same-file assumption."""
import struct

# ---------- .out synthetic test file ----------
# Record: ZX_Takt (int16 LE), ZX_Port (uint16 LE), ZX_Port_Data (uint8)
# PortMask = 0xc002; FFFD&mask == BFFD&mask? let's compute masked values.
PORT_MASK = 0xc002
FFFD_M = 0xFFFD & PORT_MASK
BFFD_M = 0xBFFD & PORT_MASK

recs = []


def rec(takt, port, data):
    recs.append(struct.pack('<hHB', takt, port, data))


# Build a sequence spanning several output ticks (MaxTStates=69888,
# OUT_TAKT_WRAP=17472 -> 4 raw "takt units" per MaxTStates interval).
# Register writes: select reg N via FFFD, then write value via BFFD.
takt = 0
regs_to_write = [(0, 0x00), (1, 0x0D), (2, 0xFE), (7, 0x38), (8, 0x0F),
                  (13, 0x0A), (0, 0x10), (1, 0x02), (10, 0x0E), (13, 0x08)]
for i, (reg, val) in enumerate(regs_to_write):
    takt = (takt + 4368) % 17472  # cycles through the 17472 wrap period
    rec(takt, 0xFFFD, reg)
    takt = (takt + 2184) % 17472
    rec(takt, 0xBFFD, val)
    if i % 3 == 1:
        # occasional "no timing info" record (ZX_Takt=-1), ignored for
        # register decode but still contributes to the T-state
        # accumulator (ZX_Takt=-1 -> ZX_Takt2=0 for that purpose).
        rec(-1, 0x0000, 0x00)
    if i % 4 == 3:
        # explicit frame-boundary marker (ZX_Takt=0)
        takt = 0
        rec(0, 0x0000, 0x00)

with open('test.out', 'wb') as f:
    f.write(b''.join(recs))

print('out file: %d records, %d bytes' % (len(recs), sum(len(r) for r in recs)))

# ---------- .epsg synthetic test file ----------
# Header (16 bytes): 'EPSG'(4) + 0x1A(1) + selector(1) [+4 bytes if 255] + pad to 16
# selector=1 -> EPSG_TStateMax=71680 (no extra 4-byte field)
header = b'EPSG' + bytes([0x1A, 1]) + b'\x00' * (16 - 6)
assert len(header) == 16

erecs = []


def erec(reg, data, tst):
    # TSt is a 3-byte LE value (top byte implicitly 0 in the real format,
    # but we only ever write valid <2^24 values here anyway).
    erecs.append(bytes([reg, data]) + struct.pack('<I', tst)[:3])


def esentinel():
    erecs.append(b'\xff' * 5)


tst = 0
epsg_regs = [(0, 0x00), (1, 0x0D), (2, 0xFE), (7, 0x38), (8, 0x0F),
             (13, 0x0A), (0, 0x10), (1, 0x02), (10, 0x0E), (13, 0x08)]
for i, (reg, val) in enumerate(epsg_regs):
    tst += 5000
    erec(reg, val, tst)
    if i % 3 == 2:
        esentinel()
        tst = 0
esentinel()  # trailing sentinel so the file ends cleanly on a frame boundary

with open('test.epsg', 'wb') as f:
    f.write(header + b''.join(erecs))

print('epsg file: header=16 + %d records, total %d bytes' %
      (len(erecs), 16 + sum(len(r) for r in erecs)))
