# TCP BBRv1 Implementation for INET 4.2.2

This repository provides a **TCP BBRv1** congestion control module compatible with **INET 4.2.2** for OMNeT++ 5.6.2.

> **Integration Notice**  
> Compiling and using this implementation requires modifications to existing base TCP files in the INET codebase, not just adding the new BBR files. Ensure you update the base files in `tcp/flavours` as described below.

---

## Directory and File Structure:

Place these files into your INET source directory at:  
`src/inet/transportlayer/tcp/flavours/`

### 1. BBR Core Files
* `TcpBBRv1.h` / `TcpBBRv1.cc`: Core BBRv1 algorithm logic, pacing, and state machine.
* * `windowed-filter.h`: Implementation of Kathleen Nichols' windowed min/max filter algorithm. Tracks the top three estimates (best, second best, third best) over a sliding time window to efficiently track metrics like minimum RTT and maximum bandwidth.

### 2. Modified INET Core Files (`tcp/flavours/`)
* `TcpSegmentTransmitInfoList.h` / `TcpSegmentTransmitInfoList.cc`: Updated the ´TcpSegmentTransmitInfoList´ method and signals to BBRv1.
---

## Installation and Setup:

1. **Backup Existing INET Source:**  
   Create a backup of your current `src/inet/transportlayer/tcp/flavours/` directory.

2. **Copy Repository Files:**  
   Copy all `.h` and `.cc` files from this repository into:
   ```bash
   <INET_ROOT>/src/inet/transportlayer/tcp/flavours/

## Clean and recompile the INET project in OMNeT++ or via terminal:

  ```bash
  cd <INET_ROOT>
  make clean
  make -j$(nproc)
```

## Usage in Simulation:
In your omnetpp.ini, configure your nodes to use TcpBBRv1:

Ini, TOML
*.host*.tcp.typename = "Tcp"
*.host*.tcp.tcpAlgorithmClass = "TcpBBRv1"
