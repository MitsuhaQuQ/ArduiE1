# Hardware tests

This directory contains diagnostic and exploratory sketches. They are retained
for regression testing and protocol investigation, but are not product
firmware.

- `eos1v_interface/`: the original UNO R4 camera communication diagnostic.

The product bridge firmware lives under `firmware/eos1v_winusb_bridge/`. The
independent PC command-line application lives in the sibling `open1V` project.
