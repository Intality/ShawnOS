#requires -Version 7.2
[CmdletBinding()]
param(
    [ValidateSet('List', 'SelfTest', 'Configure', 'Test', 'Show')]
    [string]$Mode = 'List',
    [string]$Profile = '800mhz',
    [string]$Iso,
    [string]$Root = (Join-Path $PSScriptRoot '../build/virtualbox-cpu-lab'),
    [string]$VBoxManage,
    [string]$VmName = 'ShawnOS CPU Lab',
    [ValidateRange(10, 300)]
    [int]$TimeoutSeconds = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$profilePath = Join-Path $PSScriptRoot '../tests/virtualbox-cpu-profiles.json'
$config = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json

function Get-CpuidWords([string]$Text, [int]$Length) {
    if ($Text.Length -gt $Length -or $Text -match '[^\x20-\x7e]') {
        throw 'CPUID test text must be bounded printable ASCII.'
    }
    $bytes = New-Object byte[] $Length
    $source = [Text.Encoding]::ASCII.GetBytes($Text)
    [Array]::Copy($source, $bytes, $source.Length)
    for ($offset = 0; $offset -lt $Length; $offset += 4) {
        [BitConverter]::ToUInt32($bytes, $offset)
    }
}

function Test-Profiles {
    if ($config.schema -ne 1 -or @($config.profiles).Count -eq 0) { throw 'Invalid profile schema.' }
    $names = @{}
    foreach ($item in $config.profiles) {
        if ($item.name -notmatch '^[a-z0-9-]+$' -or $names.ContainsKey($item.name)) { throw 'Invalid or duplicate profile name.' }
        $names[$item.name] = $true
        if ($item.vendor.Length -ne 12 -or $item.base_mhz -lt 0 -or $item.base_mhz -gt 65535) { throw 'Invalid CPU fields.' }
        if ($item.expected -notmatch '^(Unavailable|[1-9][0-9]{0,2} MHz|[1-9][0-9]*\.[0-9]{2} GHz)$') { throw 'Invalid expected frequency.' }
        if (@(Get-CpuidWords $item.vendor 12).Count -ne 3 -or @(Get-CpuidWords $item.brand 48).Count -ne 12) { throw 'Invalid register count.' }
    }
    $vendor = @(Get-CpuidWords 'GenuineIntel' 12)
    if ($vendor[0] -ne 0x756e6547 -or $vendor[1] -ne 0x49656e69 -or $vendor[2] -ne 0x6c65746e) { throw 'CPUID vendor byte order failed.' }
    $brand = @(Get-CpuidWords 'ABCD' 48)
    if ($brand[0] -ne 0x44434241 -or $brand[11] -ne 0) { throw 'Brand packing or padding failed.' }
    $rejected = $false
    try { Get-CpuidWords ('x' * 49) 48 | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw 'Oversized brand was accepted.' }
}

Test-Profiles
if ($Mode -eq 'SelfTest') {
    Write-Output ('VirtualBox profile self-tests passed: {0} profiles; packing, bounds, and schema.' -f @($config.profiles).Count)
    return
}
if ($Mode -eq 'List') {
    $config.profiles | Select-Object name, base_mhz, expected, vendor | Format-Table -AutoSize
    return
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'VirtualBox lab execution requires Windows.' }
if ($Profile -eq 'all') {
    if ($Mode -ne 'Test') { throw 'Configure and Show require one profile, not all.' }
    $selected = @($config.profiles)
} else {
    $selected = @($config.profiles | Where-Object { $_.name -eq $Profile })
    if ($selected.Count -ne 1) { throw 'Unknown profile. Use -Mode List to see valid names.' }
}
if (-not $Iso -or -not (Test-Path -LiteralPath $Iso -PathType Leaf)) { throw 'Supply -Iso with an existing bootable ISO.' }
$Iso = (Resolve-Path -LiteralPath $Iso).Path
$isoHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Iso).Hash.ToLowerInvariant()
if (-not $VBoxManage) { $VBoxManage = Join-Path $env:ProgramFiles 'Oracle/VirtualBox/VBoxManage.exe' }
if (-not (Test-Path -LiteralPath $VBoxManage -PathType Leaf)) { throw 'VBoxManage was not found.' }
$Root = [IO.Path]::GetFullPath($Root)
if ($Root.TrimEnd('\') -eq [IO.Path]::GetPathRoot($Root).TrimEnd('\')) { throw 'The lab root cannot be a drive root.' }
[IO.Directory]::CreateDirectory($Root) | Out-Null
$ownerPath = Join-Path $Root 'owner.json'
$vbox = New-Object -ComObject VirtualBox.VirtualBox
if ($vbox.Version -notmatch '^7\.2\.') { throw 'This runner requires VirtualBox 7.2.' }
# MachineState values in the VirtualBox 7.2 API; these differ from older SDKs.
$machinePoweredOff = 1
$machineRunning = 6

function Invoke-VBox([string[]]$Arguments, [string]$SaveOutput) {
    $display = @($Arguments | ForEach-Object { "'" + $_.Replace("'", "''") + "'" }) -join ' '
    Write-Host ('WINDOWS VBoxManage {0}' -f $display)
    $lines = @(& $VBoxManage @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($SaveOutput) {
        $lines | Out-File -LiteralPath $SaveOutput -Encoding utf8
        Write-Host ('  Output saved: {0}' -f $SaveOutput)
    } else {
        $lines | ForEach-Object { Write-Host $_ }
    }
    if ($exitCode -ne 0) { throw ('VBoxManage failed ({0}): {1}' -f $exitCode, ($lines -join "`n")) }
}

if (Test-Path -LiteralPath $ownerPath) {
    $owner = Get-Content -LiteralPath $ownerPath -Raw | ConvertFrom-Json
    if ($owner.kind -ne 'ShawnOSCpuLab' -or $owner.schema -ne 1) { throw 'Unrecognized ownership manifest.' }
    $matches = @($vbox.Machines | Where-Object { $_.Id -eq $owner.vm_id })
    if ($matches.Count -ne 1) { throw 'The owned lab VM is missing; use a new root/name instead of replacing another VM.' }
    $machine = $matches[0]
    $prefix = $Root.TrimEnd('\') + '\'
    if ($machine.Name -ne $VmName -or -not $machine.SettingsFilePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'VM name/path does not match the owned lab. No VM will be changed.'
    }
} else {
    if (@($vbox.Machines | Where-Object { $_.Name -eq $VmName }).Count) { throw 'A VM with this name already exists and is not owned by this lab.' }
    $machinesRoot = Join-Path $Root 'machines'
    if (Test-Path -LiteralPath (Join-Path $machinesRoot $VmName)) { throw 'An unowned VM directory already exists.' }
    Invoke-VBox -Arguments @('createvm', '--name', $VmName, '--basefolder', $machinesRoot, '--ostype', 'Other_64', '--register')
    $machine = $vbox.FindMachine($VmName)
    [pscustomobject]@{kind='ShawnOSCpuLab'; schema=1; vm_id=$machine.Id} | ConvertTo-Json | Set-Content -LiteralPath $ownerPath -Encoding utf8
}
$vmId = $machine.Id
if ([int]$machine.State -ne $machinePoweredOff) { throw 'The CPU lab must be powered off. Close it normally before switching profiles; saved states are not resumed.' }
Invoke-VBox -Arguments @('modifyvm', $vmId, '--memory', '64', '--cpus', '1', '--cpu-execution-cap', '100', '--ioapic', 'on', '--graphicscontroller', 'vboxvga', '--vram', '16', '--mouse', 'ps2', '--keyboard', 'ps2', '--nic1', 'none', '--boot1', 'dvd', '--boot2', 'none', '--boot3', 'none', '--boot4', 'none', '--uart1', '0x3f8', '4')
if (@($machine.StorageControllers | Where-Object { $_.Name -eq 'Boot ISO' }).Count -eq 0) {
    Invoke-VBox -Arguments @('storagectl', $vmId, '--name', 'Boot ISO', '--add', 'ide', '--controller', 'PIIX4')
}
Invoke-VBox -Arguments @('storageattach', $vmId, '--storagectl', 'Boot ISO', '--port', '0', '--device', '0', '--type', 'dvddrive', '--medium', $Iso)
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
$runRoot = Join-Path (Join-Path $Root 'runs') $runId
[IO.Directory]::CreateDirectory($runRoot) | Out-Null

function Set-Cpuid([uint32]$Leaf, [uint32[]]$Registers) {
    $values = @($Registers | ForEach-Object { '0x{0:x8}' -f $_ })
    Invoke-VBox -Arguments (@('modifyvm', $vmId, '--cpuid-set', ('0x{0:x8}' -f $Leaf)) + $values)
}

function Wait-Serial([string]$Path, [string]$Marker) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $text = ''
        if ([IO.File]::Exists($Path)) {
            try {
                $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
                $reader = [IO.StreamReader]::new($stream)
                try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
            } catch [IO.IOException] { }
        }
        if ($text.Contains($Marker)) { return $text }
        if ($text.Contains('MYOS: panic halted')) { throw 'The test guest panicked. See serial.log.' }
        if ([int]$machine.State -ne $machineRunning) { throw 'The test VM stopped before the expected serial marker.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw ('Timed out waiting for {0}; see {1}' -f $Marker, $Path)
}

$results = @()
foreach ($item in $selected) {
    if ([int]$machine.State -ne $machinePoweredOff) { throw 'Refusing to change a running or saved VM.' }
    $caseRoot = Join-Path $runRoot $item.name
    [IO.Directory]::CreateDirectory($caseRoot) | Out-Null
    $serial = Join-Path $caseRoot 'serial.log'
    Write-Host ('PROFILE {0}: expected {1}; same ISO {2}' -f $item.name, $item.expected, $isoHash)
    Invoke-VBox -Arguments @('modifyvm', $vmId, '--cpuid-remove-all', '--uart-mode1', 'file', $serial)
    $vendor = @(Get-CpuidWords $item.vendor 12)
    Set-Cpuid 0 @([uint32]0x16, $vendor[0], $vendor[2], $vendor[1])
    Set-Cpuid 0x16 @([uint32]$item.base_mhz, [uint32]0, [uint32]0, [uint32]0)
    Set-Cpuid 0x80000000L @([uint32]0x80000004L, [uint32]0, [uint32]0, [uint32]0)
    $brand = @(Get-CpuidWords $item.brand 48)
    for ($index = 0; $index -lt 3; $index++) {
        Set-Cpuid ([uint32](0x80000002L + $index)) $brand[($index * 4)..($index * 4 + 3)]
    }
    if ($Mode -eq 'Configure') {
        Write-Output ('Configured {0} as {1}; VM remains powered off.' -f $VmName, $item.name)
        return
    }
    if ($Mode -eq 'Show') {
        Invoke-VBox -Arguments @('startvm', $vmId, '--type', 'gui')
        Write-Output 'The lab is running visibly. Close it and power it off before choosing another profile.'
        return
    }
    $started = $false
    $errorText = $null
    $actual = $null
    try {
        Invoke-VBox -Arguments @('startvm', $vmId, '--type', 'headless')
        $started = $true
        Wait-Serial $serial 'MYOS: desktop ready' | Out-Null
        Wait-Serial $serial 'MYOS: boot ok ' | Out-Null
        Start-Sleep -Milliseconds 300
        Invoke-VBox -Arguments @('controlvm', $vmId, 'screenshotpng', (Join-Path $caseRoot 'about.png'))
        Invoke-VBox -Arguments @('controlvm', $vmId, 'keyboardputscancode', '3b', 'bb')
        Wait-Serial $serial 'MYOS: open Terminal' | Out-Null
        Invoke-VBox -Arguments @('controlvm', $vmId, 'keyboardputscancode', '2e', 'ae', '19', '99', '16', '96', '1c', '9c')
        $text = Wait-Serial $serial 'MYOS: command cpu'
        $reported = [regex]::Match($text, '(?m)^MYOS: CPU base speed: ([^\r\n]+)')
        if (-not $reported.Success) { throw 'The guest did not emit a formatted CPU base-speed report.' }
        $actual = $reported.Groups[1].Value
        if ($actual -ne $item.expected) { throw ('CPU mismatch: expected {0}, received {1}' -f $item.expected, $actual) }
        Invoke-VBox -Arguments @('controlvm', $vmId, 'screenshotpng', (Join-Path $caseRoot 'terminal.png'))
        Write-Host ('PASS {0}: {1}' -f $item.name, $actual)
    } catch {
        $errorText = $_.Exception.Message
        Write-Host ('FAIL {0}: {1}' -f $item.name, $errorText)
        if ($started -and [int]$machine.State -eq $machineRunning) {
            try { Invoke-VBox -Arguments @('controlvm', $vmId, 'screenshotpng', (Join-Path $caseRoot 'failure.png')) } catch { }
        }
    } finally {
        if ($started -and [int]$machine.State -eq $machineRunning) { Invoke-VBox -Arguments @('controlvm', $vmId, 'poweroff') }
    }
    $results += [pscustomobject]@{profile=$item.name; expected=$item.expected; actual=$actual; passed=($null -eq $errorText); error=$errorText; evidence=$caseRoot}
    [pscustomobject]@{vm_name=$VmName; vm_id=$vmId; iso=$Iso; iso_sha256=$isoHash; results=$results} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'result.json') -Encoding utf8
    if ($errorText) { throw ('CPU lab failed; evidence: {0}' -f $runRoot) }
}
Write-Output ('VirtualBox CPU tests passed: {0}; evidence: {1}' -f $results.Count, $runRoot)
