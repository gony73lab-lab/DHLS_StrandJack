# COG (Center of Gravity) – LS XGB / XGT TCP 2004

## Scope and source

Integrated into the existing pressure-balanced lift-up C++17 package without changing the PLC program. The ENERPAC EVO Operating Manual describes COG selection, reference cylinder, X/Y positions and cylinder loads (p.60) and the COG operating procedure (p.92). The manual does **not** specify the mathematical formula, pressure-to-load scaling, PLC COG addresses, or safety validation. The formula and software architecture below are **engineering design additions**, not claimed as verbatim manufacturer algorithms.

### Added files

| File | Purpose |
|---|---|
| `COGCalculator.h` | General variable-N projected COG from calibrated vertical support loads or pressure+area; no PLC access |
| `COGIntegration.h` | Connects existing `PressureBalancedLiftUp::Result` to COG calculator; rejects invalid/missing pressure channels |
| `COGDiagnosticPipeline.h` | Connects existing `DRegionDiagnostics::IReadOnlyDWords` (e.g. `XgbXgtTcpReadWrite`) directly to COG; reads D11–D26 + D56; does not depend on PC mode or stroke calibration |
| `test_cog.cpp`, `test_cog_pipeline.cpp` | Offline unit tests |
| `example_cog.cpp` | Simulated pressure monitor integration example |

## Formulas and units

For selected load-bearing supports (i=1..N), with known support coordinates (x_i,y_i) and vertical reaction F_i:

- Total supported force (kN): `F_total = sum(F_i)`
- Projected COG X: `x_COG = sum(F_i * x_i) / F_total`
- Projected COG Y: `y_COG = sum(F_i * y_i) / F_total`
- Load share (%): `100 * F_i / F_total`
- When only pressure is available: `F_i(kN) ≈ P_i(MPa) * A_eff,i(mm²) / 1000 - tare_i(kN)`.

The pressure-to-load equation is an approximation. It is **not** a reliable load measurement without verifying hydraulic circuit, effective pressure area, cylinder orientation, back pressure, friction, mechanical preload, pressure transducer calibration, and whether all external vertical reactions are measured. For double-acting cylinders, the relevant differential pressure and annular area must be handled explicitly; this implementation accepts only an already validated effective pressure-area model. Static vertical reaction equilibrium is assumed; dynamic lifting, side loads, other support contacts, or unmeasured load paths can make COG inaccurate. Calculated COG is the **horizontal projection** of the gravity center; height (Z) cannot be inferred from these measurements.

## D-map and cylinder count

The supplied `8POINT ADDRESS(1).xlsx` defines D11,D13,...D25 as 8 pressure channels, D56.0..7 as pressure sensor disconnect bits. `COGDiagnosticPipeline` reads the 16-word block D11..D26 plus D56 using the existing TCP 2004 XGT read driver; it performs **no writes**. `COGCalculator` itself accepts any number of supports, but the existing D map limits direct PLC acquisition to 8 channels. A verified extended D map is required for 9+ cylinders.

## Live integration example (code snippet; do NOT use on machinery without validation)

```cpp
#include "COGDiagnosticPipeline.h"
#include "XgbXgtTcpReadWrite.h"

COGDiagnosticPipeline::Config cfg;
cfg.referenceCylinderId = 0;
// Fill cfg.channels with 3..8 verified channel definitions:
// cylinderId, xMm, yMm, geometryVerified,
// mpaPerCount, pressureOffsetMpa, pressureSigned16,
// pressureCalibrationVerified, effectiveAreaMm2, areaVerified, tareKN.
COGDiagnosticPipeline cog(cfg);
XgbXgtTcpReadWrite plc("192.0.2.10", 2004); // Documentation-only IP
if (plc.Connect()) {
    // Use a monotonic clock for all three timestamps.
    // const auto result = cog.Poll(plc, beforeMs, afterMs, nowMs);
    // Only display COG when result.valid() == true.
}
```

For reuse of an existing pressure-balanced monitor result:

```cpp
// pressureResult = pressureMonitor.Analyze(snapshot, sampledAtMs, nowMs);
// geometry = verified coordinates + effective cylinder areas.
// auto cog = COGIntegration().Analyze(pressureResult, geometry, referenceCylinderId);
// if (cog.inputUsable) display(cog.cog.cogXMm, cog.cog.cogYMm);
```

### Validation rules

- At least 3 distinct, non-collinear **load-bearing** supports for a 2D estimate.
- Reference cylinder must be in the selected set; coordinates must share a verified reference frame.
- Pressure conversion, effective area and geometry require explicit verification flags; NaN/inf, negative forces, duplicate channels, missing selected channels and invalid geometry reject the estimate.
- Sensor-disconnect D56 flags, failed TCP read and stale acquisition reject the live estimate.
- Sequential PLC D-word reads are **not atomic**; acquisition time is bounded but coherence under motion must be verified against the PLC.
- COG calculation never writes D50/D51/D52 and never changes the existing `SynchronizationAlgorithm` or pressure-balancing commands.
- COG is advisory. Do not use it to command motion or as an independent safety function.

## Offline verification

Linux/macOS with g++:

```sh
g++ -std=c++17 -Wall -Wextra -pedantic test_cog.cpp -o test_cog && ./test_cog
g++ -std=c++17 -Wall -Wextra -pedantic test_cog_pipeline.cpp -o test_cog_pipeline && ./test_cog_pipeline
g++ -std=c++17 -Wall -Wextra -pedantic example_cog.cpp -o example_cog && ./example_cog
```

Visual Studio 2022: compile the above source files as a C++17 Console project; the XGT driver uses Winsock2 and requires `Ws2_32.lib` (already linked via pragma). No PLC is contacted by the tests.

## Synthetic demonstration

Four supports at (0,0), (2000,0), (2000,1000), (0,1000) mm, with vertical reactions 100,100,300,100 kN:

- Total = 600 kN
- COG relative to cylinder 1 = (1333.33, 666.67) mm
- Cylinder 3 bears 50% of measured total support force

The numerical values are hypothetical and are **not** sensor calibration values or site setpoints.
