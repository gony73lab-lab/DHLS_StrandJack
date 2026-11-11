@echo off
setlocal EnableDelayedExpansion
where cl.exe >nul 2>&1
if not "!errorlevel!"=="0" (
  echo Run from Visual Studio x64 Developer Command Prompt or Developer PowerShell.
  exit /b 1
)
set "evoRoot=%~dp0..\Evo"
set "serviceSource=%~dp0..\EquipmentControlService.cpp"
set "testOutput=%TEMP%\DHLS-offline-%RANDOM%-%RANDOM%"
mkdir "%testOutput%"
if not "!errorlevel!"=="0" exit /b 1
pushd "%testOutput%"
for %%T in (test test_xgt test_write test_cog test_cog_pipeline test_lowering test_pressure_balanced test_pressure_lift) do (
  cl /nologo /EHsc /std:c++17 "%evoRoot%\%%T.cpp" /Fe"%%T.exe" >"%%T.log" 2>&1
  if not "!errorlevel!"=="0" (type "%%T.log" & goto failed)
  ".\%%T.exe"
  if not "!errorlevel!"=="0" goto failed
  echo PASS: %%T
)
cl /nologo /EHsc /std:c++17 "%~dp0test_equipment_service.cpp" "%serviceSource%" /Fe"test_equipment_service.exe" >"test_equipment_service.log" 2>&1
if not "!errorlevel!"=="0" (type "test_equipment_service.log" & goto failed)
".\test_equipment_service.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_xgt_deadline.cpp" /Fe"test_xgt_deadline.exe" >"test_xgt_deadline.log" 2>&1
if not "!errorlevel!"=="0" (type "test_xgt_deadline.log" & goto failed)
".\test_xgt_deadline.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_emergency_transport.cpp" /Fe"test_emergency_transport.exe" >"test_emergency_transport.log" 2>&1
if not "!errorlevel!"=="0" (type "test_emergency_transport.log" & goto failed)
".\test_emergency_transport.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_simulation.cpp" "%serviceSource%" /Fe"test_simulation.exe" >"test_simulation.log" 2>&1
if not "!errorlevel!"=="0" (type "test_simulation.log" & goto failed)
".\test_simulation.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_sequence_engine.cpp" /Fe"test_sequence_engine.exe" >"test_sequence_engine.log" 2>&1
if not "!errorlevel!"=="0" (type "test_sequence_engine.log" & goto failed)
".\test_sequence_engine.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_plc_map.cpp" "%serviceSource%" /Fe"test_plc_map.exe" >"test_plc_map.log" 2>&1
if not "!errorlevel!"=="0" (type "test_plc_map.log" & goto failed)
".\test_plc_map.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_control_commands.cpp" "%serviceSource%" /Fe"test_control_commands.exe" >"test_control_commands.log" 2>&1
if not "!errorlevel!"=="0" (type "test_control_commands.log" & goto failed)
".\test_control_commands.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_units.cpp" /Fe"test_units.exe" >"test_units.log" 2>&1
if not "!errorlevel!"=="0" (type "test_units.log" & goto failed)
".\test_units.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_fault_interlock.cpp" "%serviceSource%" /Fe"test_fault_interlock.exe" >"test_fault_interlock.log" 2>&1
if not "!errorlevel!"=="0" (type "test_fault_interlock.log" & goto failed)
".\test_fault_interlock.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_scale_consistency.cpp" "%serviceSource%" /Fe"test_scale_consistency.exe" >"test_scale_consistency.log" 2>&1
if not "!errorlevel!"=="0" (type "test_scale_consistency.log" & goto failed)
".\test_scale_consistency.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_pc_write_gate.cpp" /Fe"test_pc_write_gate.exe" >"test_pc_write_gate.log" 2>&1
if not "!errorlevel!"=="0" (type "test_pc_write_gate.log" & goto failed)
".\test_pc_write_gate.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_manual_ports.cpp" "%serviceSource%" /Fe"test_manual_ports.exe" >"test_manual_ports.log" 2>&1
if not "!errorlevel!"=="0" (type "test_manual_ports.log" & goto failed)
".\test_manual_ports.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_settings_runtime.cpp" "%serviceSource%" /Fe"test_settings_runtime.exe" >"test_settings_runtime.log" 2>&1
if not "!errorlevel!"=="0" (type "test_settings_runtime.log" & goto failed)
".\test_settings_runtime.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_distance_deceleration.cpp" /Fe"test_distance_deceleration.exe" >"test_distance_deceleration.log" 2>&1
if not "!errorlevel!"=="0" (type "test_distance_deceleration.log" & goto failed)
".\test_distance_deceleration.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_automatic_plc.cpp" "%serviceSource%" /Fe"test_automatic_plc.exe" >"test_automatic_plc.log" 2>&1
if not "!errorlevel!"=="0" (type "test_automatic_plc.log" & goto failed)
".\test_automatic_plc.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_equipment_session.cpp" "%serviceSource%" /Fe"test_equipment_session.exe" >"test_equipment_session.log" 2>&1
if not "!errorlevel!"=="0" (type "test_equipment_session.log" & goto failed)
".\test_equipment_session.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_pressure_auto.cpp" /Fe"test_pressure_auto.exe" >"test_pressure_auto.log" 2>&1
if not "!errorlevel!"=="0" (type "test_pressure_auto.log" & goto failed)
".\test_pressure_auto.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_pressure_auto_service.cpp" "%serviceSource%" /Fe"test_pressure_auto_service.exe" >"test_pressure_auto_service.log" 2>&1
if not "!errorlevel!"=="0" (type "test_pressure_auto_service.log" & goto failed)
".\test_pressure_auto_service.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_pressure_boundary.cpp" /Fe"test_pressure_boundary.exe" >"test_pressure_boundary.log" 2>&1
if not "!errorlevel!"=="0" (type "test_pressure_boundary.log" & goto failed)
".\test_pressure_boundary.exe"
if not "!errorlevel!"=="0" goto failed
cl /nologo /EHsc /std:c++17 "%~dp0test_pressure_interlocks.cpp" /Fe"test_pressure_interlocks.exe" >"test_pressure_interlocks.log" 2>&1
if not "!errorlevel!"=="0" (type "test_pressure_interlocks.log" & goto failed)
".\test_pressure_interlocks.exe"
if not "!errorlevel!"=="0" goto failed
echo All 28 offline tests passed. Artifacts: %testOutput%
popd
exit /b 0
:failed
popd
exit /b 1
