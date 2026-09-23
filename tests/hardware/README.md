# Hardware tests

This directory contains diagnostic and exploratory sketches. They are retained
for regression testing and protocol investigation, but are not product
firmware.

- `eos1v_interface/`: the original UNO R4 camera communication diagnostic.

The single-character commands documented in `docs/command-reference.md` belong
only to `eos1v_interface`. They are not implemented by the product bridge.

The product bridge firmware lives under `firmware/eos1v_winusb_bridge/`. The
independent PC command-line applications live in the sibling `open1V-cli` and
`open1v-filmdb` projects.
