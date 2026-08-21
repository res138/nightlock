[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$StageDir,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$AppVersion,

    # Optional overrides make local/reproducible packaging possible without
    # depending on the caller's PATH. CI normally gets QtBinDir from
    # QT_ROOT_DIR and VCRedistPath from the Visual Studio installation.
    [string]$QtBinDir,

    [string]$VCRedistPath
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version Latest

function Assert-FileExists {
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

function Find-WinDeployQt {
    param([string]$PreferredBinDir)

    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($PreferredBinDir)) {
        $candidates.Add((Join-Path $PreferredBinDir 'windeployqt.exe'))
    }
    if (-not [string]::IsNullOrWhiteSpace($env:QT_ROOT_DIR)) {
        $candidates.Add((Join-Path $env:QT_ROOT_DIR 'bin\windeployqt.exe'))
    }

    $command = Get-Command 'windeployqt.exe' -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $candidates.Add($command.Source)
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw @'
windeployqt.exe was not found. Pass -QtBinDir or set QT_ROOT_DIR to the
Qt installation used to build Nightlock. Refusing to produce an installer
whose Qt DLL/plugin closure cannot be verified.
'@
}

function Find-VCRedist {
    param(
        [string]$PreferredPath,
        [string]$ResolvedStageDir
    )

    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($PreferredPath)) {
        $candidates.Add($PreferredPath)
    }

    # Some Qt versions place the official redistributable beside the deployed
    # executable when compiler-runtime deployment is enabled.
    $candidates.Add((Join-Path $ResolvedStageDir 'vc_redist.x64.exe'))

    if (-not [string]::IsNullOrWhiteSpace($env:VCToolsRedistDir)) {
        $candidates.Add((Join-Path $env:VCToolsRedistDir 'vc_redist.x64.exe'))
    }

    # Developer Command Prompt environments commonly expose one of these even
    # when VCToolsRedistDir itself is absent.
    $redistRoots = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($env:VCINSTALLDIR)) {
        $redistRoots.Add((Join-Path $env:VCINSTALLDIR 'Redist\MSVC'))
    }
    if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
        $redistRoots.Add((Join-Path $env:VSINSTALLDIR 'VC\Redist\MSVC'))
    }
    foreach ($redistRoot in $redistRoots) {
        if (Test-Path -LiteralPath $redistRoot -PathType Container) {
            $foundUnderRoot = Get-ChildItem -LiteralPath $redistRoot `
                -Filter 'vc_redist.x64.exe' -Recurse -File |
                Sort-Object FullName -Descending
            foreach ($file in $foundUnderRoot) {
                $candidates.Add($file.FullName)
            }
        }
    }

    $programFilesX86 = [Environment]::GetFolderPath(
        [Environment+SpecialFolder]::ProgramFilesX86
    )
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
            $found = @(
                & $vswhere -latest -products '*' `
                    -requires Microsoft.VisualStudio.Component.VC.Redist.14.Latest `
                    -find 'VC\Redist\MSVC\**\vc_redist.x64.exe'
            )
            if ($LASTEXITCODE -eq 0) {
                foreach ($path in $found) {
                    if (-not [string]::IsNullOrWhiteSpace($path)) {
                        $candidates.Add($path.Trim())
                    }
                }
            }

            # Older Visual Studio manifests do not expose the Redist component
            # ID, but vswhere can still find the file in the selected instance.
            if ($found.Count -eq 0) {
                $fallback = @(
                    & $vswhere -latest -products '*' `
                        -find 'VC\Redist\MSVC\**\vc_redist.x64.exe'
                )
                if ($LASTEXITCODE -eq 0) {
                    foreach ($path in $fallback) {
                        if (-not [string]::IsNullOrWhiteSpace($path)) {
                            $candidates.Add($path.Trim())
                        }
                    }
                }
            }
        }
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw @'
The official Microsoft Visual C++ x64 Redistributable was not found.
Install the Visual Studio 2022 VC++ redistributable component or pass
-VCRedistPath. Nightlock Setup embeds and installs this prerequisite so a
clean Windows machine never has to download or install it separately.
'@
}

function Find-InnoCompiler {
    $candidates = [System.Collections.Generic.List[string]]::new()
    $programFilesX86 = [Environment]::GetFolderPath(
        [Environment+SpecialFolder]::ProgramFilesX86
    )
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $candidates.Add((Join-Path $programFilesX86 'Inno Setup 6\ISCC.exe'))
    }

    $programFiles = [Environment]::GetFolderPath(
        [Environment+SpecialFolder]::ProgramFiles
    )
    if (-not [string]::IsNullOrWhiteSpace($programFiles)) {
        $candidates.Add((Join-Path $programFiles 'Inno Setup 6\ISCC.exe'))
    }

    $command = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $candidates.Add($command.Source)
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw 'Inno Setup 6 compiler (ISCC.exe) was not found.'
}

function Assert-ReleasePayload {
    param([string]$ResolvedStageDir)

    $requiredFiles = @(
        'Nightlock.exe',
        'bin\nightlock.exe',
        'Qt6Core.dll',
        'Qt6Gui.dll',
        'Qt6Widgets.dll',
        'Qt6Svg.dll',
        'qt.conf',
        'plugins\platforms\qwindows.dll',
        'vc_redist.x64.exe'
    )
    foreach ($relativePath in $requiredFiles) {
        Assert-FileExists `
            -LiteralPath (Join-Path $ResolvedStageDir $relativePath) `
            -Description 'Required Windows runtime file'
    }

    foreach ($resourceDirectory in @('icons', 'fonts')) {
        $path = Join-Path $ResolvedStageDir $resourceDirectory
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            throw "Required resource directory is missing: $path"
        }
    }

    $qtConfigPath = Join-Path $ResolvedStageDir 'qt.conf'
    $qtConfig = Get-Content -LiteralPath $qtConfigPath -Raw
    if ($qtConfig -notmatch '(?im)^\s*Plugins\s*=\s*plugins\s*$') {
        throw "qt.conf does not select the bundled plugins directory: $qtConfigPath"
    }
    if ($qtConfig -match '(?i)(QT_ROOT_DIR|[A-Z]:\\Qt\\|_work\\nightlock)') {
        throw "qt.conf contains a build-machine path: $qtConfigPath"
    }

    $misplacedQtDll = Get-ChildItem `
        -LiteralPath (Join-Path $ResolvedStageDir 'bin') `
        -Filter 'Qt6*.dll' -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $misplacedQtDll) {
        throw @"
Qt runtime DLL is misplaced beside the CLI: $($misplacedQtDll.FullName)
Nightlock.exe can only resolve Qt DLLs from the stage/install root.
"@
    }

    $legacyPlatformPlugin = Join-Path $ResolvedStageDir 'platforms\qwindows.dll'
    if (Test-Path -LiteralPath $legacyPlatformPlugin -PathType Leaf) {
        throw @"
qwindows.dll is outside the declared plugins directory: $legacyPlatformPlugin
The supported layout is plugins\platforms\qwindows.dll with root qt.conf.
"@
    }

    # A release Setup must never accidentally contain a debug Qt or debug MSVC
    # runtime. Those files are absent on clean machines and are not
    # redistributable.
    $debugRuntimeNames = @(
        'Qt6Cored.dll',
        'Qt6Guid.dll',
        'Qt6Widgetsd.dll',
        'Qt6Svgd.dll',
        'qwindowsd.dll',
        'msvcp140d.dll',
        'vcruntime140d.dll',
        'ucrtbased.dll'
    )
    $debugRuntime = Get-ChildItem -LiteralPath $ResolvedStageDir -Recurse -File |
        Where-Object { $debugRuntimeNames -contains $_.Name } |
        Select-Object -First 1
    if ($null -ne $debugRuntime) {
        throw "Debug-only runtime found in release payload: $($debugRuntime.FullName)"
    }

    # Release CI builds libsodium statically. Looking for the PE import name in
    # both executables makes that packaging contract explicit and prevents an
    # unnoticed switch to a DLL that windeployqt would not collect.
    foreach ($relativeExecutable in @('Nightlock.exe', 'bin\nightlock.exe')) {
        $executable = Join-Path $ResolvedStageDir $relativeExecutable
        $imageText = [Text.Encoding]::ASCII.GetString(
            [IO.File]::ReadAllBytes($executable)
        )
        if (($imageText.IndexOf(
                'libsodium.dll',
                [StringComparison]::OrdinalIgnoreCase) -ge 0) -or
            ($imageText.IndexOf(
                'sodium.dll',
                [StringComparison]::OrdinalIgnoreCase) -ge 0)) {
            throw @"
$relativeExecutable imports a dynamic libsodium DLL. Release builds must use
-DNIGHTLOCK_FORCE_VENDORED_SODIUM=ON so Setup remains self-contained.
"@
        }
    }
}

function Assert-OfficialVCRedist {
    param([string]$LiteralPath)

    Assert-FileExists `
        -LiteralPath $LiteralPath `
        -Description 'Microsoft Visual C++ x64 Redistributable'

    $file = Get-Item -LiteralPath $LiteralPath
    if ($file.Length -lt 1MB) {
        throw "Visual C++ Redistributable is unexpectedly small: $LiteralPath"
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $LiteralPath
    if (($signature.Status -ne 'Valid') -or
        ($null -eq $signature.SignerCertificate) -or
        ($signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation')) {
        throw "Visual C++ Redistributable does not have a valid Microsoft signature: $LiteralPath"
    }
}

$resolvedStageDir = (Resolve-Path -LiteralPath $StageDir).Path
$guiExecutable = Join-Path $resolvedStageDir 'Nightlock.exe'
Assert-FileExists -LiteralPath $guiExecutable -Description 'Nightlock GUI executable'

# CMake's Qt deploy script is the primary deployment path. Running the Qt tool
# once more here is intentional: the installer is the final trust boundary and
# must repair/verify the payload even when it was staged by another build path.
$winDeployQt = Find-WinDeployQt -PreferredBinDir $QtBinDir
$pluginsDirectory = Join-Path $resolvedStageDir 'plugins'
& $winDeployQt `
    --dir $resolvedStageDir `
    --libdir $resolvedStageDir `
    --plugindir $pluginsDirectory `
    --release `
    --no-compiler-runtime `
    --no-translations `
    --exclude-plugins qpdf `
    $guiExecutable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE."
}

# Keep the runtime lookup layout explicit even if windeployqt changes when it
# decides to emit qt.conf. Qt DLLs live at the root and plugins below plugins\.
Copy-Item `
    -LiteralPath (Join-Path $PSScriptRoot 'qt.conf') `
    -Destination (Join-Path $resolvedStageDir 'qt.conf') `
    -Force

$resolvedVCRedist = Find-VCRedist `
    -PreferredPath $VCRedistPath `
    -ResolvedStageDir $resolvedStageDir
Assert-OfficialVCRedist -LiteralPath $resolvedVCRedist

$stagedVCRedist = Join-Path $resolvedStageDir 'vc_redist.x64.exe'
if (-not [string]::Equals(
        $resolvedVCRedist,
        $stagedVCRedist,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    Copy-Item -LiteralPath $resolvedVCRedist -Destination $stagedVCRedist -Force
}
Assert-OfficialVCRedist -LiteralPath $stagedVCRedist
Assert-ReleasePayload -ResolvedStageDir $resolvedStageDir

$innoCompiler = Find-InnoCompiler
$installerScript = Join-Path $PSScriptRoot 'nightlock.iss'

& $innoCompiler `
    "/DStageDir=$resolvedStageDir" `
    "/DAppVersion=$AppVersion" `
    $installerScript

if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup failed with exit code $LASTEXITCODE."
}
