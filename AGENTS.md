# ElectricDreams Agent Notes

## Full Windows "Development" Packaged Build

### Goal
Produce a full Win64 packaged build (compile + cook + stage + pak + prereqs + archive) for local testing.

### Prerequisites
- Unreal Engine 5.7 installed at `C:\Epic\UE_5.7`
- Project file at `D:\src\ElectricDreams\ElectricDreams.uproject`
- Run from a Developer PowerShell with enough free disk space (cook/stage can be large)

### Command (from `D:\src\ElectricDreams`)
```powershell
$EngineRoot = "C:\Epic\UE_5.7"
$Project = "D:\src\ElectricDreams\ElectricDreams.uproject"
$ArchiveDir = "D:\src\ElectricDreams\Artifacts\WindowsDev"

& "$EngineRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
  -project="$Project" `
  -noP4 -utf8output `
  -platform=Win64 -targetplatform=Win64 `
  -clientconfig=Development -serverconfig=Development `
  -build -cook -allmaps `
  -stage -pak -prereqs `
  -archive -archivedirectory="$ArchiveDir"
```

### Expected Outputs
- Packaged build under `D:\src\ElectricDreams\Artifacts\WindowsDev\`
- Launchable executable at `D:\src\ElectricDreams\Artifacts\WindowsDev\ElectricDreamsSample.exe`
- Game binaries at `D:\src\ElectricDreams\Artifacts\WindowsDev\ElectricDreams\Binaries\Win64\`
- Build/cook logs emitted in the terminal and under `Saved\Logs`

### Verification Checklist
- UAT exits with code `0`
- `D:\src\ElectricDreams\Artifacts\WindowsDev\ElectricDreamsSample.exe` exists
- App launches and loads default map without missing content errors

### Known Pitfalls
- If NVIDIA plugins fail to link, confirm these exist in the project plugin folders:
  - `Plugins/DLSS/Source/ThirdParty/NGX/Lib/x64/*.lib`
  - `Plugins/StreamlineCore/Binaries/ThirdParty/Win64/*.dll`
- If cook fails on `BP_OpalHoverDrone` with missing `/SP_HoverDrone/Input/*`, restore or regenerate those assets under `Plugins/SP_HoverDrone/Content/Input`.
