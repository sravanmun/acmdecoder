# acmdecoder

Decoder for raw binary data from the ACM (Acquisition and Control Module) board. Takes a `.bin` file produced by the DAQ and returns level 0 / 1 / 2 frames as zero-copy NumPy / pandas views.

# Requirements

- **OS:** Linux x86_64 with glibc ≥ 2.28 (CentOS / RHEL / AlmaLinux 8+, Debian 10+, Ubuntu 18.10+, Fedora 28+, Arch) or macOS 13+ (Intel or Apple Silicon). Alpine Linux (musl) and Linux aarch64 are not currently supported by prebuilt wheels.
- **Python:** 3.10, 3.11, 3.12, 3.13, or 3.14

# Installation

```bash
pip install acmdecoder
```

## Building from source

From a checkout of the repo:
```bash
pip install .
```

Source builds require a **C++20 compiler**. All other native dependencies are fetched and built automatically by CMake (one-time network access on first build).


# Usage

## Quick start
```python
from acmdecoder import Decoder

# auto-detects a sibling <stem>.meta file if present
dec = Decoder("path/to/data.bin")

df0 = dec.df0_view  # level 0 (raw samples)
df1 = dec.df1_view  # level 1 (per-skip)
df2 = dec.df2_view  # level 2 (CDS / CTS reductions)
```

The `*_view` properties are zero-copy views into the underlying C++ buffers — their lifetime is tied to the `Decoder` instance, so don't let it go out of scope while the views are still in use.

## Constructors
Three forms are available:

```python
# 1. Short form — auto-detects sibling <stem>.meta
Decoder(binfile, debug=0, write_log=False)

# 2. Full form — explicit geometry, no .meta lookup
Decoder(fname, debug=0, write_log=False,
        nrow=0, ncol=0, ndcm=1, nint=0)

# 3. From an explicit (json, bin) pair
Decoder(jsonfile, binfile, debug=0, write_log=False)
```

## Available data
**Frames** (pandas DataFrames):
- `df0_view` — level 0: per-sample clock/control bits and ADC value
- `df1_view` — level 1: per-skip sums (`pix`, `line`, `skip`, `n`, `sum`, ...)
- `df2_view` — level 2: per-pixel CDS / CTS reductions (`cds_sum`, `cds_sum2`, `cts_sum`, ...)

**1D NumPy views** (level 2 derived quantities):
- `cds_skp_view`, `cds_avg_view`, `cds_var_view`
- `cts_skp_view`, `cts_avg_view`, `cts_var_view`

**Metadata**: `filename`, `nrow`, `ncol`, `ndcm`, `nadc`

**Info word fields**: `info_word`, `RESERVE_BIT`, `ITP_FIRMWARE`, `LVL`, `LVL1_SIZE`, `LVL2_SIZE`, `LV_FREQ`

**Error counters**: `error_acc`, `error_idx`, `error_cin`, `error_nadc`

**Sequence counters**: `cclki`, `cclke`, `cepix`, `cline`, `cpong`, `start`, `busy`, `end`
