[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$StageDir,

    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$ExpectedVersion
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-File {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LiteralPath,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        throw "$Description is missing: $LiteralPath"
    }
}

function Assert-PayloadLayout {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [switch]$RequireVCRedist
    )

    $requiredFiles = @(
        'Nightlock.exe',
        'bin\nightlock.exe',
        'Qt6Core.dll',
        'Qt6Gui.dll',
        'Qt6Widgets.dll',
        'Qt6Svg.dll',
        'qt.conf',
        'plugins\platforms\qwindows.dll'
    )
    if ($RequireVCRedist) {
        $requiredFiles += 'vc_redist.x64.exe'
    }

    foreach ($relativePath in $requiredFiles) {
        Assert-File `
            -LiteralPath (Join-Path $Root $relativePath) `
            -Description 'Required packaged runtime file'
    }

    foreach ($resourceDirectory in @('icons', 'fonts')) {
        $path = Join-Path $Root $resourceDirectory
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            throw "Required resource directory is missing: $path"
        }
    }

    $qtConfigPath = Join-Path $Root 'qt.conf'
    $qtConfig = Get-Content -LiteralPath $qtConfigPath -Raw
    if ($qtConfig -notmatch '(?im)^\s*Plugins\s*=\s*plugins\s*$') {
        throw "qt.conf does not point Nightlock at its bundled plugins: $qtConfigPath"
    }
    if ($qtConfig -match '(?i)(QT_ROOT_DIR|[A-Z]:\\Qt\\|github\\workspace|_work\\nightlock)') {
        throw "qt.conf leaks a build-machine path: $qtConfigPath"
    }

    # Nightlock.exe is installed at the root. Qt DLLs in bin\ cannot be found
    # by the Windows loader on a clean machine and caused the v1.2.2 failure.
    $misplacedQtDll = Get-ChildItem -LiteralPath (Join-Path $Root 'bin') `
        -Filter 'Qt6*.dll' -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $misplacedQtDll) {
        throw "Qt runtime DLL is misplaced beside the CLI: $($misplacedQtDll.FullName)"
    }

    $legacyPlatformPlugin = Join-Path $Root 'platforms\qwindows.dll'
    if (Test-Path -LiteralPath $legacyPlatformPlugin -PathType Leaf) {
        throw @"
qwindows.dll was deployed outside the plugins directory: $legacyPlatformPlugin
This can hide a broken qt.conf/plugin layout on the build runner.
"@
    }
}

function Find-DumpBin {
    $command = Get-Command 'dumpbin.exe' -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $searchRoot = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022'
    $candidates = @(
        Get-ChildItem `
            -Path "$searchRoot\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" `
            -File -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending
    )
    if ($candidates.Count -eq 0) {
        throw 'dumpbin.exe was not found on the Windows build runner.'
    }
    return $candidates[0].FullName
}

function Assert-PEDependencyClosure {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $dumpBin = Find-DumpBin
    $rootDlls = @{}
    Get-ChildItem -LiteralPath $Root -Filter '*.dll' -File | ForEach-Object {
        $rootDlls[$_.Name.ToLowerInvariant()] = $_.FullName
    }

    $objects = @(
        (Get-Item -LiteralPath (Join-Path $Root 'Nightlock.exe'))
        (Get-Item -LiteralPath (Join-Path $Root 'bin\nightlock.exe'))
    )
    $objects += Get-ChildItem -LiteralPath $Root -Filter 'Qt6*.dll' -File
    $objects += Get-Item -LiteralPath (Join-Path $Root 'plugins\platforms\qwindows.dll')

    foreach ($object in $objects) {
        $output = & $dumpBin /nologo /dependents $object.FullName 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "dumpbin failed for $($object.FullName):`n$output"
        }

        foreach ($line in $output) {
            if ($line -notmatch '^\s+([A-Za-z0-9_.+-]+\.dll)\s*$') {
                continue
            }
            $dependency = $Matches[1]
            $key = $dependency.ToLowerInvariant()
            $besideObject = Join-Path $object.DirectoryName $dependency
            $systemCopy = Join-Path $env:SystemRoot "System32\$dependency"
            $isApiSet = $dependency -match '^(?i)(api-ms-win-|ext-ms-win-)'
            if (-not $rootDlls.ContainsKey($key) -and
                -not (Test-Path -LiteralPath $besideObject -PathType Leaf) -and
                -not (Test-Path -LiteralPath $systemCopy -PathType Leaf) -and
                -not $isApiSet) {
                throw "$($object.Name) has an unresolved PE dependency: $dependency"
            }
        }
    }
}

$resolvedStageDir = (Resolve-Path -LiteralPath $StageDir).Path
$resolvedInstaller = (Resolve-Path -LiteralPath $InstallerPath).Path
Assert-PayloadLayout -Root $resolvedStageDir -RequireVCRedist

$redistPath = Join-Path $resolvedStageDir 'vc_redist.x64.exe'
$redist = Get-Item -LiteralPath $redistPath
if ($redist.Length -lt 1MB) {
    throw "vc_redist.x64.exe is unexpectedly small: $($redist.Length) bytes"
}
$redistSignature = Get-AuthenticodeSignature -LiteralPath $redistPath
if (($redistSignature.Status -ne 'Valid') -or
    ($null -eq $redistSignature.SignerCertificate) -or
    ($redistSignature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation')) {
    throw 'The staged VC++ redistributable does not have a valid Microsoft signature.'
}

# Inno 6.7 Setup executables are not a supported 7-Zip archive format. The
# authoritative final-artifact check is the silent installation below:
# PrepareToInstall must extract and execute the embedded prerequisite, and the
# Setup log must record that exact file. Inno compilation itself also fails if
# the non-external Source file named by the script is unavailable.
$installer = Get-Item -LiteralPath $resolvedInstaller
if ($installer.Length -lt 1MB) {
    throw "Generated Setup is unexpectedly small: $($installer.Length) bytes"
}

$smokeRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'nightlock-windows-package-smoke-' + [Guid]::NewGuid().ToString('N')
)
$installDir = Join-Path $smokeRoot 'installed\Nightlock'
$installerLog = Join-Path $smokeRoot 'setup.log'
New-Item -ItemType Directory -Path $smokeRoot -Force | Out-Null

$installerArguments = @(
    '/VERYSILENT',
    '/SUPPRESSMSGBOXES',
    '/NORESTART',
    '/RESTARTEXITCODE=3010',
    '/SP-',
    "/DIR=`"$installDir`"",
    "/LOG=`"$installerLog`"",
    '/MERGETASKS="!desktopicon,!addtopath"'
)
$installProcess = Start-Process `
    -FilePath $resolvedInstaller `
    -ArgumentList $installerArguments `
    -Wait -PassThru
if ($installProcess.ExitCode -notin @(0, 3010)) {
    if (Test-Path -LiteralPath $installerLog) {
        Get-Content -LiteralPath $installerLog -Tail 200 | Write-Host
    }
    throw "Silent Setup failed with exit code $($installProcess.ExitCode)."
}

Assert-PayloadLayout -Root $installDir
Assert-PEDependencyClosure -Root $installDir

if (-not (Test-Path -LiteralPath $installerLog -PathType Leaf)) {
    throw 'Inno Setup did not produce the requested installation log.'
}
if ((Get-Content -LiteralPath $installerLog -Raw) -notmatch '(?i)vc_redist\.x64\.exe') {
    throw 'Setup log does not show the embedded VC++ runtime prerequisite.'
}

# Remove every Qt development hint before launching from the installed tree.
# Otherwise the CI Qt SDK can mask a missing packaged DLL or plugin.
$qtEnvironmentNames = @(
    Get-ChildItem Env: |
        Where-Object { $_.Name -match '^(?i)(QT|QML)' } |
        ForEach-Object { $_.Name }
)
foreach ($name in $qtEnvironmentNames) {
    Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
}
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$env:USERPROFILE = Join-Path $smokeRoot 'home'
$env:APPDATA = Join-Path $env:USERPROFILE 'AppData\Roaming'
$env:LOCALAPPDATA = Join-Path $env:USERPROFILE 'AppData\Local'
New-Item -ItemType Directory -Path $env:APPDATA, $env:LOCALAPPDATA -Force | Out-Null

$cliPath = Join-Path $installDir 'bin\nightlock.exe'
$cliOutput = & $cliPath --version 2>&1
if (($LASTEXITCODE -ne 0) -or
    (($cliOutput -join "`n").Trim() -ne "nightlock $ExpectedVersion")) {
    throw "Installed CLI smoke test failed: $cliOutput"
}

$screenshotPath = Join-Path $smokeRoot 'nightlock-gui.png'
$env:NIGHTLOCK_DEMO = '1'
$env:NIGHTLOCK_SCREENSHOT = $screenshotPath
$env:NIGHTLOCK_SCREENSHOT_DELAY = '1000'
$guiPath = Join-Path $installDir 'Nightlock.exe'
$guiProcess = Start-Process `
    -FilePath $guiPath `
    -WorkingDirectory $installDir `
    -PassThru
if (-not $guiProcess.WaitForExit(30000)) {
    $guiProcess.Kill()
    throw 'Installed GUI did not finish its deterministic smoke run within 30 seconds.'
}
if ($guiProcess.ExitCode -ne 0) {
    throw "Installed GUI exited with code $($guiProcess.ExitCode)."
}
Assert-File -LiteralPath $screenshotPath -Description 'Installed GUI smoke screenshot'
if ((Get-Item -LiteralPath $screenshotPath).Length -eq 0) {
    throw 'Installed GUI smoke screenshot is empty.'
}

Write-Host "Windows package smoke test passed for Nightlock $ExpectedVersion."
