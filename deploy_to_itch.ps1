<#
One-command release of the Windows download of ABEP: the orbit view (Godot) and abep_run.exe, with a launcher that runs the
settings in settings.txt. It builds abep_run.exe with MSVC (the C++ runtime linked in, so that no Visual C++ redistributable is
needed), exports the viewer, writes the Godot licence notice, puts the files together, tests what it made, and pushes it to
itch.io with butler (which only uploads what changed, and makes the patches the itch.io app downloads). Page:
https://overby-industries.itch.io/ion-propulsion-simulator

Usage, from any folder:
    powershell -File deploy_to_itch.ps1             # build, test, push
    powershell -File deploy_to_itch.ps1 -NoPush     # everything except publishing; the download is left in viewer\builds\windows-release

It publishes to a public page: run it only when you mean to release. -NoPush is the way to check that it works.

Needs:
  - Godot 4.6.3, with its Windows export templates installed (Editor > Manage Export Templates).
  - Visual Studio's C++ tools (MSVC), found with vswhere.
  - butler, logged in as an itch.io account that may push to the page (butler login), unless you use -NoPush.
Each of Godot and butler is looked up, in this order, as the parameter (-GodotExe, -ButlerExe), the environment variable
(GODOT_EXE, BUTLER_EXE), the PATH, and the usual install folders (a Godot_v4.6* folder under Downloads, the itch app's butler).

The version shown on the itch.io page is the version in include\abep\version.hpp, a dash and the short hash of the commit (and
-dirty if there are uncommitted changes). Nothing is written back into the repository, apart from the .uid files Godot may
write next to new scripts: commit those with the change that made them.
#>
param(
    [switch]$NoPush,
    [string]$GodotExe,
    [string]$ButlerExe
)

$ErrorActionPreference = "Stop"

# What is specific to this project.
$ItchGame = "overby-industries/ion-propulsion-simulator"
$Channel = "windows"
$ItchTarget = "${ItchGame}:$Channel"
$PageUrl = "https://overby-industries.itch.io/ion-propulsion-simulator"
$ExeName = "ABEPOrbitView.exe"
$GodotVersionPrefix = "4.6.3"

$RepoRoot = $PSScriptRoot
$ProjectDir = Join-Path $RepoRoot "viewer"
$PackageDir = Join-Path $ProjectDir "package"
$BuildDir = Join-Path $ProjectDir "builds\windows-release"
$BuildExe = Join-Path $BuildDir $ExeName
$AbepRun = Join-Path $BuildDir "abep_run.exe"
$Launcher = "Simulate and view.bat"

function Find-Tool {
    param([string]$Label, [string]$Given, [string]$EnvName, [string[]]$Commands, [string[]]$Patterns)
    if ($Given) {
        if (Test-Path -LiteralPath $Given) { return (Resolve-Path -LiteralPath $Given).Path }
        throw "$Label was not found at '$Given' (the parameter)."
    }
    $fromEnv = [Environment]::GetEnvironmentVariable($EnvName)
    if ($fromEnv) {
        if (Test-Path -LiteralPath $fromEnv) { return (Resolve-Path -LiteralPath $fromEnv).Path }
        throw "$Label was not found at '$fromEnv' (the environment variable $EnvName)."
    }
    foreach ($c in $Commands) {
        $found = Get-Command $c -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { return $found.Source }
    }
    foreach ($p in $Patterns) {
        $hit = Get-ChildItem -Path $p -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1
        if ($hit) { return $hit.FullName }
    }
    throw "Could not find $Label. Pass it as a parameter, set `$env:$EnvName, or put it on the PATH."
}

function Invoke-Checked {
    param([string]$What, [scriptblock]$Command)
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "$What failed with exit code $LASTEXITCODE." }
}

# Runs a program, with its output captured and its input closed (so that a `pause` cannot wait for a key), and returns the exit
# code and the text. Start-Process -PassThru can leave ExitCode empty in Windows PowerShell 5.1, so this goes through .NET.
function Invoke-Captured {
    param([string]$File, [string]$Arguments, [string]$WorkingDirectory, [int]$TimeoutSeconds = 120, [hashtable]$Environment = @{})
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $File
    $startInfo.Arguments = $Arguments
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($name in $Environment.Keys) { $startInfo.EnvironmentVariables[$name] = [string]$Environment[$name] }
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $process.StandardInput.Close()
    $out = $process.StandardOutput.ReadToEndAsync()
    $err = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        throw "$File did not finish within $TimeoutSeconds s."
    }
    $process.WaitForExit()
    return [pscustomobject]@{ ExitCode = $process.ExitCode; Text = "$($out.Result)`n$($err.Result)" }
}

# Copies a text file with Windows line endings, whatever the checkout has, replacing {{NAME}} marks.
function Copy-AsText {
    param([string]$From, [string]$To, [hashtable]$Replace = @{})
    $text = [System.IO.File]::ReadAllText($From)
    foreach ($key in $Replace.Keys) { $text = $text.Replace($key, [string]$Replace[$key]) }
    $text = ($text -replace "`r`n", "`n") -replace "`n", "`r`n"
    [System.IO.File]::WriteAllText($To, $text, (New-Object System.Text.UTF8Encoding($false)))
}

# The numbers of the first data row of a samples.csv, by column name, and the number of samples.
function Read-Samples {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { throw "$Path was not written." }
    $lines = @([System.IO.File]::ReadAllLines($Path) | Where-Object { $_.Trim() -ne "" })
    $header = $lines[0].Split(",")
    if ($header.Count -ne 17 -or $header[0] -ne "t_s" -or $header[14] -ne "x_km" -or $header[16] -ne "z_km") {
        throw "$Path does not have the 17 columns of samples.csv (it has $($header.Count): $($lines[0]))."
    }
    $first = $lines[1].Split(",")
    $row = @{}
    for ($i = 0; $i -lt $header.Count; $i++) { $row[$header[$i]] = $first[$i] }
    return [pscustomobject]@{ Count = $lines.Count - 1; First = $row }
}

# --- Tools ------------------------------------------------------------------------------------------------------------------

$Godot = Find-Tool "Godot" $GodotExe "GODOT_EXE" @("godot.console", "godot") @(
    "$env:ProgramFiles\Godot*\*console*.exe",
    "$HOME\Downloads\Godot_v4.6*\*console*.exe",
    "$env:LOCALAPPDATA\Godot*\*console*.exe")
$Butler = $null
if (-not $NoPush) {
    $Butler = Find-Tool "butler" $ButlerExe "BUTLER_EXE" @("butler") @(
        "$env:APPDATA\itch\apps\butler\butler.exe",
        "$env:LOCALAPPDATA\butler\butler.exe")
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$VcVars = $null
if (Test-Path -LiteralPath $vswhere) {
    $vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if ($vsRoot) {
        $candidate = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path -LiteralPath $candidate) { $VcVars = $candidate }
    }
}
if (-not $VcVars) { throw "Visual Studio's C++ tools (MSVC) were not found: install the 'Desktop development with C++' workload." }

$godotVersion = (& $Godot --version | Select-Object -First 1)
if (-not "$godotVersion".StartsWith($GodotVersionPrefix)) {
    throw "This project is built for Godot $GodotVersionPrefix but '$Godot' says '$godotVersion'."
}
Write-Host "Godot $godotVersion"

# --- The version label ------------------------------------------------------------------------------------------------------

$versionHeader = Get-Content -LiteralPath (Join-Path $RepoRoot "include\abep\version.hpp") -Raw
if ($versionHeader -notmatch 'return "(\d+\.\d+\.\d+)"') { throw "Could not read the version from include\abep\version.hpp." }
$AbepVersion = $Matches[1]
$UserVersion = $AbepVersion
if (Get-Command git -ErrorAction SilentlyContinue) {
    $hash = (& git -C $RepoRoot rev-parse --short HEAD | Select-Object -First 1)
    $dirty = & git -C $RepoRoot status --porcelain
    if ($hash) { $UserVersion = "$AbepVersion-$hash" }
    if ($dirty) {
        $UserVersion = "$UserVersion-dirty"
        Write-Host "Warning: there are uncommitted changes, and the build will include them (the version says -dirty)."
    }
}
$presetText = Get-Content -LiteralPath (Join-Path $ProjectDir "export_presets.cfg") -Raw
if ($presetText -notmatch ('application/product_version="' + [regex]::Escape($AbepVersion) + '"')) {
    Write-Host "Warning: viewer\export_presets.cfg has another product_version than $AbepVersion (include\abep\version.hpp). Update it."
}
Write-Host "Version label: $UserVersion"

# --- 1. abep_run.exe, with MSVC ---------------------------------------------------------------------------------------------

if (Test-Path -LiteralPath $BuildDir) {
    if (-not $BuildDir.EndsWith("builds\windows-release")) { throw "Refusing to clear '$BuildDir'." }
    Remove-Item -LiteralPath $BuildDir -Recurse -Force   # no stale files from an earlier build in what gets uploaded
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Write-Host "Building abep_run.exe with MSVC..."
$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ("abep_release_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
try {
    $buildCmd = Join-Path $workDir "build.cmd"
    @(
        "@echo off",
        "call `"$VcVars`" >nul || exit /b 1",
        "cd /d `"$workDir`"",
        "cl /nologo /std:c++17 /O2 /MT /EHsc /W4 /permissive- /utf-8 /I `"$RepoRoot\include`" `"$RepoRoot\examples\abep_run.cpp`" /Fe:`"$AbepRun`" || exit /b 1",
        "dumpbin /nologo /dependents `"$AbepRun`""
    ) | Set-Content -LiteralPath $buildCmd -Encoding ASCII
    $built = Invoke-Captured "cmd.exe" "/c `"$buildCmd`"" $workDir 900
    if ($built.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $AbepRun)) {
        Write-Host $built.Text
        throw "The MSVC build of abep_run.exe failed."
    }
    # The project builds without warnings at /W4; keep it that way.
    if ($built.Text -match "warning C\d+") {
        Write-Host $built.Text
        throw "The MSVC build of abep_run.exe printed warnings."
    }
    $dependencies = ($built.Text -split "`r?`n") | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() }
    $runtime = $dependencies | Where-Object { $_ -match '^(VCRUNTIME|MSVCP|api-ms-win-crt|ucrtbase|CONCRT|VCOMP)' }
    if ($runtime) { throw "abep_run.exe needs the C++ runtime DLLs ($($runtime -join ', ')): it would not start on a PC without them." }
    Write-Host ("abep_run.exe is built, without warnings, and needs only: " + ($dependencies -join ", "))

    # --- 2. The viewer, exported ------------------------------------------------------------------------------------------
    # The first import of a fresh checkout can end with a crash code from inside Godot after it has finished (seen with
    # GDExtension projects); running it twice, and judging by the export, is the way round that.
    Write-Host "Importing the project..."
    & $Godot --headless --path $ProjectDir --import
    & $Godot --headless --path $ProjectDir --import
    Write-Host "Exporting the viewer..."
    # --path is required: Godot does not look for a project, and $ProjectDir is the project whatever folder this is run from.
    Invoke-Checked "The Godot export" { & $Godot --headless --path $ProjectDir --export-release "Windows Desktop" $BuildExe }
    if (-not (Test-Path -LiteralPath $BuildExe)) { throw "The export did not produce $BuildExe." }

    # --- 3. The rest of the download -------------------------------------------------------------------------------------
    Write-Host "Writing the Godot licence notice..."
    $godotLicences = Join-Path $BuildDir "licenses\Godot.txt"
    New-Item -ItemType Directory -Force -Path (Split-Path $godotLicences) | Out-Null
    # --quit-after is a safety stop: a script error before the script quits would otherwise leave Godot waiting.
    Invoke-Checked "The licence notice" { & $Godot --headless --path $ProjectDir --quit-after 600 -s res://tools/dump_godot_licenses.gd -- $godotLicences }
    if (-not (Test-Path -LiteralPath $godotLicences) -or (Get-Item -LiteralPath $godotLicences).Length -lt 10000) {
        throw "The Godot licence notice was not written, or is too short."
    }

    Copy-AsText (Join-Path $PackageDir $Launcher) (Join-Path $BuildDir $Launcher)
    Copy-AsText (Join-Path $PackageDir "README.txt") (Join-Path $BuildDir "README.txt") @{ "{{VERSION}}" = $UserVersion }
    Copy-AsText (Join-Path $RepoRoot "LICENSE") (Join-Path $BuildDir "LICENSE.txt")

    # settings.txt is made from what abep_run itself says its settings are, so that it cannot go out of date.
    $help = Invoke-Captured $AbepRun "--help" $BuildDir 30
    $settingLines = foreach ($line in ($help.Text -split "`r?`n")) {
        if ($line -match '^\s{2}(\w+) = (\S+)\s+(.*)$') { "# {0}={1}    {2}" -f $Matches[1], $Matches[2], $Matches[3] }
    }
    if (@($settingLines).Count -lt 30) { throw "abep_run.exe --help listed only $(@($settingLines).Count) settings." }
    $settingsHeader = @(
        "# Settings for abep_run.exe, used by 'Simulate and view.bat'.",
        "#",
        "# A line that does not start with # is a setting: a name, an = sign and a number, with no spaces. To change a",
        "# setting, take the # off the front of its line, change the number after the = sign, and save the file. A note may",
        "# follow the number. What the lines say after the number is the meaning and the unit. A setting left as it is has",
        "# the default shown. The defaults are illustrative, not data about any real satellite or thruster.",
        "#",
        "# For example, a satellite of 150 kg at 300 km for three days:",
        "#     altitude_km=300",
        "#     mass_kg=150",
        "#     duration_days=3",
        "#"
    )
    $settingsText = (($settingsHeader + $settingLines) -join "`r`n") + "`r`n"
    [System.IO.File]::WriteAllText((Join-Path $BuildDir "settings.txt"), $settingsText, (New-Object System.Text.UTF8Encoding($false)))

    # --- 4. Test what was made ---------------------------------------------------------------------------------------------
    Write-Host "Test 1: abep_run.exe runs and writes the three files..."
    $testRun = Join-Path $workDir "run"
    $result = Invoke-Captured $AbepRun "duration_days=0.05 out=`"$testRun`"" $workDir 120
    if ($result.ExitCode -ne 0) { Write-Host $result.Text; throw "abep_run.exe exited with code $($result.ExitCode)." }
    foreach ($file in "samples.csv", "orbits.csv", "summary.csv") {
        if (-not (Test-Path -LiteralPath (Join-Path $testRun $file))) { throw "abep_run.exe did not write $file." }
    }
    $samples = Read-Samples (Join-Path $testRun "samples.csv")
    if ($samples.Count -ne 72) { throw "A run of 0.05 days should have 72 samples; it has $($samples.Count)." }

    Write-Host "Test 2: the exported viewer starts, finds its bundled sample and quits cleanly..."
    $viewer = Invoke-Captured $BuildExe "--headless --quit-after 300" $BuildDir 120
    if ($viewer.ExitCode -ne 0) { Write-Host $viewer.Text; throw "The viewer exited with code $($viewer.ExitCode)." }
    if ($viewer.Text -match "ERROR") { Write-Host $viewer.Text; throw "The viewer printed errors." }
    $loaded = [regex]::Match($viewer.Text, 'ABEP orbit view: loaded res://sample/samples\.csv \((\d+) samples\)')
    if (-not $loaded.Success -or [int]$loaded.Groups[1].Value -lt 100) {
        Write-Host $viewer.Text
        throw "The viewer did not load its bundled sample."
    }

    Write-Host "Test 3: the exported viewer plays a run that abep_run.exe wrote..."
    $viewer = Invoke-Captured $BuildExe "--headless --quit-after 120 -- `"$testRun`"" $BuildDir 120
    if ($viewer.ExitCode -ne 0 -or $viewer.Text -match "ERROR") { Write-Host $viewer.Text; throw "The viewer failed on a run of abep_run.exe." }
    if ($viewer.Text -notmatch 'ABEP orbit view: loaded .*samples\.csv \(72 samples\)') { Write-Host $viewer.Text; throw "The viewer did not load the run." }

    Write-Host "Test 4: the launcher, with the settings as shipped..."
    # ABEP_NO_VIEWER makes the launcher stop after the simulation, so that no window opens. It is called by its full path, as
    # Explorer does, and with `call`, because cmd /c strips the quotes from a quoted name that has spaces in it.
    $launched = Invoke-Captured "cmd.exe" "/c call `"$BuildDir\$Launcher`"" $BuildDir 120 @{ ABEP_NO_VIEWER = "1" }
    if ($launched.ExitCode -ne 0) { Write-Host $launched.Text; throw "The launcher exited with code $($launched.ExitCode)." }
    $samples = Read-Samples (Join-Path $BuildDir "my_run\samples.csv")
    if ([double]$samples.First["altitude_km"] -ne 250.0 -or $samples.Count -lt 1000) {
        throw "The shipped settings should give a day at 250 km; the run has $($samples.Count) samples starting at $($samples.First['altitude_km']) km."
    }
    Remove-Item -LiteralPath (Join-Path $BuildDir "my_run") -Recurse -Force   # not part of the download

    Write-Host "Test 5: the launcher with settings of a user's: a note, a leading space, a comment line..."
    $userDir = Join-Path $workDir "user"
    New-Item -ItemType Directory -Force -Path $userDir | Out-Null
    Copy-Item -LiteralPath $AbepRun, (Join-Path $BuildDir $Launcher) -Destination $userDir
    $userSettings = "# a comment`r`naltitude_km=300      my own note after the number`r`n   sample_every=12`r`n`r`nduration_days=0.05`r`n"
    [System.IO.File]::WriteAllText((Join-Path $userDir "settings.txt"), $userSettings, (New-Object System.Text.UTF8Encoding($false)))
    $launched = Invoke-Captured "cmd.exe" "/c call `"$userDir\$Launcher`"" $userDir 120 @{ ABEP_NO_VIEWER = "1" }
    if ($launched.ExitCode -ne 0) { Write-Host $launched.Text; throw "The launcher exited with code $($launched.ExitCode) on a user's settings." }
    $samples = Read-Samples (Join-Path $userDir "my_run\samples.csv")
    if ([double]$samples.First["altitude_km"] -ne 300.0 -or $samples.Count -ne 36) {
        throw "A user's settings (300 km, every 12th step, 0.05 days) should give 36 samples from 300 km; got $($samples.Count) from $($samples.First['altitude_km']) km."
    }

    Write-Host "Test 6: the launcher says which setting is wrong..."
    [System.IO.File]::WriteAllText((Join-Path $userDir "settings.txt"), "bogus_setting=1`r`n", (New-Object System.Text.UTF8Encoding($false)))
    $launched = Invoke-Captured "cmd.exe" "/c call `"$userDir\$Launcher`"" $userDir 120 @{ ABEP_NO_VIEWER = "1" }
    if ($launched.ExitCode -eq 0 -or $launched.Text -notmatch "unknown setting 'bogus_setting'") {
        Write-Host $launched.Text
        throw "The launcher should fail, naming the setting, on a setting that does not exist."
    }
} finally {
    if ($workDir -and (Test-Path -LiteralPath $workDir)) { Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue }
}

# Only what belongs in the download is in the folder.
$expected = @("ABEPOrbitView.exe", "abep_run.exe", $Launcher, "settings.txt", "README.txt", "LICENSE.txt", "licenses\Godot.txt")
$found = Get-ChildItem -LiteralPath $BuildDir -Recurse -File | ForEach-Object { $_.FullName.Substring($BuildDir.Length + 1) }
$unexpected = $found | Where-Object { $expected -notcontains $_ }
$missing = $expected | Where-Object { $found -notcontains $_ }
if ($unexpected -or $missing) { throw "The download is not what it should be. Unexpected: [$($unexpected -join ', ')]. Missing: [$($missing -join ', ')]." }
Write-Host "The tests passed. The download holds:"
Get-ChildItem -LiteralPath $BuildDir -Recurse -File | ForEach-Object { Write-Host ("  {0,-26} {1,10:N1} KiB" -f $_.FullName.Substring($BuildDir.Length + 1), ($_.Length / 1KB)) }

if ($NoPush) {
    $size = (Get-ChildItem -LiteralPath $BuildDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
    Write-Host ("Not pushed (-NoPush). The download is in {0} ({1:N1} MiB). To release it: powershell -File deploy_to_itch.ps1" -f $BuildDir, ($size / 1MB))
    return
}

# --- 5. Push ----------------------------------------------------------------------------------------------------------------
# The live build is read BEFORE the push so that the poll below has something to compare against: `butler status` can return
# the previous build for a few seconds after a push while itch.io processes the new one.
function Get-LiveBuild {
    $line = & $Butler status $ItchGame --json 2>$null | Select-String '"type":"result"' | Select-Object -Last 1
    if (-not $line) { return $null }
    $status = $line.ToString() | ConvertFrom-Json
    return ($status.value.channels | Where-Object { $_.name -eq $Channel }).head
}

$before = Get-LiveBuild
Write-Host "Pushing to itch.io ($ItchTarget, version $UserVersion)..."
Invoke-Checked "butler push" { & $Butler push $BuildDir $ItchTarget --userversion $UserVersion }

Write-Host "Waiting for itch.io to finish processing the new build..."
$build = $null
for ($i = 0; $i -lt 20; $i++) {
    $build = Get-LiveBuild
    if ($build -and (-not $before -or $build.id -ne $before.id)) { break }
    Start-Sleep -Seconds 3
}
if (-not $build -or ($before -and $build.id -eq $before.id)) {
    throw "Timed out waiting for the new build in 'butler status $ItchGame'. Check it on the page."
}
if ($build.state -ne "completed") {
    Write-Host "Warning: build $($build.id) is still '$($build.state)', not 'completed'."
}
Write-Host "Done. Live: build #$($build.version), version $UserVersion, at $PageUrl"
Write-Host "Commit any .uid files Godot wrote next to new scripts (git status shows them)."
