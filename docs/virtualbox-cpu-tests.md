# VirtualBox CPU Reporting Lab

These are simulated CPU reports, not physical clock-speed controls. Every profile
boots the same ISO. Test values live in `tests/virtualbox-cpu-profiles.json`, never
in the kernel. CPUID overrides do not emulate a complete Intel or AMD processor.

## Requirements

- Windows with PowerShell 7.2 or newer and VirtualBox 7.2.
- A ShawnOS ISO built in Ubuntu with `make iso` and copied to Windows.
- Keep the lab VM powered off before changing profiles. Saved states are rejected.

The runner creates only its own `ShawnOS CPU Lab` VM: one CPU, 64 MB RAM, PS/2
input, no network, no hard disk, and a 100% processing cap. Existing VM names are
never reused without the matching ownership manifest and settings path. Other
VMs and host security settings are not changed. The runner never deletes a VM.

## Profiles

| Profile | Report supplied by the VM | Expected display |
| --- | --- | --- |
| `800mhz` | Explicit brand suffix: 0.80 GHz | `800 MHz` |
| `999mhz` | Explicit brand suffix: 0.999 GHz | `999 MHz` |
| `1000mhz` | Explicit brand suffix: 1.00 GHz | `1.00 GHz` |
| `3600mhz` | Explicit brand suffix: 3.60 GHz | `3.60 GHz` |
| `brand-ghz` | Missing leaf value; explicit brand suffix | `1.80 GHz` |
| `brand-mhz` | Missing leaf value; explicit brand suffix | `800 MHz` |
| `unavailable` | Neither source supplies a frequency | `Unavailable` |
| `amd-no-firmware` | AMD vendor with no valid firmware startup-speed report | `Unavailable` |

The last profile verifies that missing data does not turn an Intel-style brand
suffix into a guessed AMD base frequency. AMD and other vendors can instead use
the vendor-independent firmware path, covered by `make cpu-test`; see
[CPU compatibility](cpu-support.md). These are not complete CPU hardware emulations.

In the tested Windows VirtualBox 7.2.20 setup, a leaf 0x16 override was accepted
but omitted from the guest's CPUID table. These live profiles therefore exercise
the Intel brand fallback. Unit tests cover frequency-leaf decoding and its
priority; the VirtualBox matrix does not claim to boot-test that backend.

## Run

From a Windows copy of the repository, or an exported bundle retaining `scripts/`
and `tests/`, use PowerShell 7. Set `$iso` to the copied ISO and `$lab` to a dedicated
output directory:

```powershell
$iso = 'C:\ShawnOS\build\myos-x86_64.iso'
$lab = 'C:\ShawnOS\build\virtualbox-cpu-lab'
./scripts/virtualbox-cpu-test.ps1 -Mode List
./scripts/virtualbox-cpu-test.ps1 -Mode SelfTest
./scripts/virtualbox-cpu-test.ps1 -Mode Test -Profile all -Iso $iso -Root $lab
```

`Test` runs headlessly without controlling the host mouse or keyboard. It waits
for the desktop, captures About, opens Terminal through the test VM's own keyboard
API, runs `cpu`, checks the exact serial report, and powers off only the VM it
started. Failures stop the matrix and retain evidence. Each run records the ISO
SHA-256, actual and expected readings, serial logs, and PNG screenshots under
`$lab/runs/`. Run the lab separately from anyone using its GUI.

To inspect a profile yourself:

```powershell
./scripts/virtualbox-cpu-test.ps1 -Mode Show -Profile 800mhz -Iso $iso -Root $lab
```

Close the lab and choose **Power Off**, not Save State, before selecting another
profile. `-Mode Configure` applies one profile but leaves the VM powered off for
manual startup in VirtualBox. Changing profiles does not rebuild or alter the ISO.

CI runs `SelfTest` to validate the profile schema and CPUID byte packing without
requiring VirtualBox or nested virtualization. Live VM evidence is produced by
local `Test` runs, not implied by the CI self-test.
