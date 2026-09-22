# EOS-1V Protocol Validation Results

The following operations have been completed on real hardware:

- `FF/F4/F6/F1` session establishment, asynchronous F4 handling, and both F2 exits;
- camera ID read and `0 -> 12 -> 0` write/restore;
- BCD clock read, write, checksum and read-back;
- four C.Fn reads and current-bank writes;
- all 16 P.Fn reads, selected parameter writes and restoration;
- E7/E8/E9 shooting-field widths 08, 10 and 20 with mask validation;
- E1/E3/E4 multi-roll iteration, variable record lengths, limits and checksums;
- controlled delete-all and empty-database download;
- bounded retries for late bytes, zero replies and asynchronous F4;
- offline malformed-frame and safety-mask tests.

## Final restored audit

```text
F1: ID=0
E8: FF FF 0C 3F 00 08 7F 00 (width class 20)
E1: 0000 (zero roll segments)
FC: FA 00
DD trailing byte: 00
C.Fn-19: 01 (option 0)
```

## Empty database fix

The first empty-database run incorrectly sent E3 after `E1=0000`, then waited
for a 36-byte header and failed to cleanly exit. The firmware now skips E3/E4
when the roll count is zero, reports zero rolls/records, and exits with F2/F2.
Unestablished sessions still release the drivers without sending F2.

## Continuous session result

The original Remote behavior was reproduced in one physical PC session:

1. read C.Fn;
2. use an in-session FF/F4/F1 boundary and read P.Fn;
3. read F3/A1/D1;
4. read E8/FC/E1;
5. send only one final F2.

The first 300 ms version overlapped a late byte and failed. Using the original
approximately 3.6 s action interval and acknowledging asynchronous F4 made the
full sequence pass. This confirms that F2 belongs to final shutdown, not to
each high-level action.

## Remaining validation

The meanings of A1, reserved D1/P.Fn bytes, some E4 flags, E2's physical erase
scope, oscilloscope waveforms, and PCB tolerances remain open. USB identity and
transport experiments target UNO R4 Minima; ESP32-S3 is outside project scope.
