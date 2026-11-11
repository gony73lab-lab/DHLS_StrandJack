# Pressure-balanced lift-up monitoring extension (C++17)

This extension is **diagnostic only**, NOT closed-loop pressure control. It never writes D51 or D52, never generates valve commands, and never substitutes for PLC interlocks or safety systems.

## D-map
Selected cylinder index i (0..7): pressure raw word D(11+2*i), sensor fault bit D56.i. Stroke values D(12+2*i) and fault D59.i are separately processed by DRegionDiagnostics. The attached 8POINT address map documents 8 channels. For >8 physical cylinders a verified new address map is required; this code rejects indexes >=8.

## Calculation
P_i = raw_i * verified_mpaPerCount_i + offsetMpa_i
mean = sum(P_i)/N
spread = max(P_i)-min(P_i)
channel deviation = P_i - mean
target observed for ALL selected channels when P_i >= target_i - targetToleranceMpa
imbalance when spread > maxSpreadMpa (advisory)
limit indicated when any P_i > advisoryMaxMpa_i (advisory, highest precedence)

Target observed does NOT imply lift completed, safe pressure, or motion command termination. Equal pressures do not guarantee equal forces (cylinder areas differ), equal strokes, or safe load distribution. No pressure balance correction is attempted; individual valve control semantics are unknown.

## Integration
Use existing XgbXgtTcpReadWrite or XgbXgtTcpReadOnly to poll D-region into DRegionDiagnostics::Snapshot. Construct PressureBalancedLiftUp::Config with 1..8 selected channels, each with verified raw->MPa calibration, verified target and advisory ceiling, and site-approved advisory spread tolerance. Call Analyze(snapshot, monotonicAcquisitionEndMs, monotonicNowMs) and show/log results. Do not use this monitor output to automatically write PLC motion bits. Check snapshot.usable / diagnostic.dataValid for stroke diagnostics; these are independent from pressure validity.

## Required commissioning before closed-loop control
Verify PLC ladder, hydraulic manifold and per-cylinder valve control, D51.1 command level/pulse/acknowledgement, D52 selection ownership and sequencing, pump prerequisites, emergency stops, pressure sensor calibration, stale data and coherent acquisition, pressure and stroke protection, physical independent overpressure and load-holding protections. Read-modify-write of D51/D52 is non-atomic. Existing D-map has no verified pressure setpoint/mode handshake. Closed-loop pressure equalization cannot be claimed without proving an appropriate PLC-supported control interface and independent safety review.

Offline test: `g++ -std=c++17 -Wall -Wextra -pedantic test_pressure_balanced.cpp -o test_pressure_balanced && ./test_pressure_balanced`
