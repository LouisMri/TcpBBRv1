# TCP BBRv1 for OMNeT++ INET

This repository contains the revised TCP BBRv1 congestion control implementation for **INET 4.2.2** and **OMNeT++ 5.6.2**. The source keeps the public class name `TcpBBRv1`. It includes the later pacing-timer send path, delivery-rate sampling and loss-recovery changes used in our BBRv1 validation.

## Files

| File | Purpose |
|---|---|
| `TcpBBRv1.cc`, `TcpBBRv1.h` | BBRv1 controller, pacing send path and TCP event handling. |
| `TcpSegmentTransmitInfoList.cc`, `TcpSegmentTransmitInfoList.h` | Per-segment transmission information used for delivery-rate sampling. |
| `windowed-filter.h` | Windowed bandwidth and RTT filters. |

## Required INET integration

These five files alone are **not** a drop-in addition to an unmodified INET 4.2.2 tree. The pacing path requires a compatible TCP base implementation: `TcpBaseAlg.h` and `TcpBaseAlg.cc` must provide the `pacingTimer` lifecycle and callback, and `TcpConnection` must provide the compatible send interface. Integrate those changes in your INET installation before building this module.

## Install in a compatible INET checkout

From the root of this repository, set `INET_ROOT` to a separate checkout that already has the required TCP base integration, then copy the five source files:

```bash
INET_ROOT=/path/to/compatible/inet
cp TcpBBRv1.cc TcpBBRv1.h \
   TcpSegmentTransmitInfoList.cc TcpSegmentTransmitInfoList.h \
   windowed-filter.h \
   "$INET_ROOT/src/inet/transportlayer/tcp/flavours/"
```

With the OMNeT++ 5.6.2 environment loaded, regenerate the INET makefiles and build:

```bash
cd "$INET_ROOT"
make makefiles
make -j"$(nproc)"
```

Select the algorithm in the simulation's `omnetpp.ini` for the sending TCP endpoint, for example:

```ini
*.client.tcp.typename = "Tcp"
*.client.tcp.tcpAlgorithmClass = "TcpBBRv1"
```

## Authors and license

- Luigi Martino
- Kai-Steffen Hielscher
- Reinhard German

Friedrich-Alexander-Universität Erlangen-Nürnberg (FAU).

The repository's `LICENSE` file contains GNU GPL version 3 text. The BBR source-file headers specify `LGPL-3.0-or-later`; the adapted helper files retain their own LGPL notices and original attribution. Keep the file-specific notices with any redistributed source.
