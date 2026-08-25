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

function Assert-NonEmptyPng {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LiteralPath,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    Assert-File -LiteralPath $LiteralPath -Description $Description
    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    $pngSignature = [byte[]]@(137, 80, 78, 71, 13, 10, 26, 10)
    if ($bytes.Length -le $pngSignature.Length) {
        throw "$Description is empty or truncated: $LiteralPath"
    }
    for ($index = 0; $index -lt $pngSignature.Length; $index++) {
        if ($bytes[$index] -ne $pngSignature[$index]) {
            throw "$Description is not a PNG file: $LiteralPath"
        }
    }
}

function Initialize-WindowsResourceReader {
    if ($null -ne ('Nightlock.PackageAudit.NativeResourceReader' -as [type])) {
        return
    }

    # LoadLibraryEx with data/image-resource flags reads PE resources without
    # resolving imports or executing any code from the package under test.
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace Nightlock.PackageAudit
{
    public static class NativeResourceReader
    {
        private const uint LoadLibraryAsDataFile = 0x00000002;
        private const uint LoadLibraryAsImageResource = 0x00000020;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode,
            ExactSpelling = true, SetLastError = true)]
        private static extern IntPtr LoadLibraryExW(
            string fileName,
            IntPtr file,
            uint flags);

        [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)]
        private static extern IntPtr FindResourceW(
            IntPtr module,
            IntPtr name,
            IntPtr type);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint SizeofResource(
            IntPtr module,
            IntPtr resourceInfo);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LoadResource(
            IntPtr module,
            IntPtr resourceInfo);

        [DllImport("kernel32.dll")]
        private static extern IntPtr LockResource(IntPtr resourceData);

        [DllImport("kernel32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FreeLibrary(IntPtr module);

        [DllImport("shcore.dll")]
        private static extern int GetProcessDpiAwareness(
            IntPtr process,
            out int awareness);

        public static byte[] ReadResource(
            string fileName,
            int resourceType,
            int resourceId)
        {
            IntPtr module = LoadLibraryExW(
                fileName,
                IntPtr.Zero,
                LoadLibraryAsDataFile | LoadLibraryAsImageResource);
            if (module == IntPtr.Zero)
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "Unable to load PE resources from " + fileName);

            try
            {
                IntPtr resourceInfo = FindResourceW(
                    module,
                    new IntPtr(resourceId),
                    new IntPtr(resourceType));
                if (resourceInfo == IntPtr.Zero)
                    throw new Win32Exception(
                        Marshal.GetLastWin32Error(),
                        "PE resource " + resourceType + "/" + resourceId +
                        " is missing from " + fileName);

                uint size = SizeofResource(module, resourceInfo);
                IntPtr resourceData = LoadResource(module, resourceInfo);
                IntPtr resourceBytes = LockResource(resourceData);
                if (size == 0 || resourceData == IntPtr.Zero ||
                    resourceBytes == IntPtr.Zero)
                    throw new Win32Exception(
                        Marshal.GetLastWin32Error(),
                        "Unable to read PE resource " + resourceType + "/" +
                        resourceId + " from " + fileName);

                byte[] bytes = new byte[size];
                Marshal.Copy(resourceBytes, bytes, 0, checked((int)size));
                return bytes;
            }
            finally
            {
                FreeLibrary(module);
            }
        }

        public static int ReadProcessDpiAwareness(IntPtr process)
        {
            int awareness;
            int result = GetProcessDpiAwareness(process, out awareness);
            if (result != 0)
                Marshal.ThrowExceptionForHR(result);
            return awareness;
        }
    }
}
'@
}

function Assert-NightlockWindowsIdentity {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LiteralPath,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedVersion
    )

    Assert-File -LiteralPath $LiteralPath -Description 'Nightlock GUI executable'
    Initialize-WindowsResourceReader

    $versionInfo = (Get-Item -LiteralPath $LiteralPath).VersionInfo
    $expectedFields = @{
        CompanyName = 'Nightlock contributors'
        FileDescription = 'Nightlock password manager'
        FileVersion = "$ExpectedVersion.0"
        OriginalFilename = 'Nightlock.exe'
        ProductName = 'Nightlock'
        ProductVersion = $ExpectedVersion
    }
    foreach ($field in $expectedFields.Keys) {
        $actual = ([string]$versionInfo.$field).Trim()
        if (-not [string]::Equals(
                $actual,
                [string]$expectedFields[$field],
                [StringComparison]::Ordinal)) {
            throw @"
Nightlock.exe has incorrect $field metadata: '$actual'
Expected '$($expectedFields[$field])'.
"@
        }
    }

    # RT_MANIFEST = 24; application manifests use resource ID 1.
    $manifestBytes = [Nightlock.PackageAudit.NativeResourceReader]::ReadResource(
        $LiteralPath, 24, 1)
    $manifestText = [Text.Encoding]::UTF8.GetString($manifestBytes)
    try {
        [xml]$manifest = $manifestText
    }
    catch {
        throw "Nightlock.exe contains malformed application-manifest XML: $_"
    }

    $expectedManifestSettings = @{
        dpiAware = 'true/pm'
        dpiAwareness = 'PerMonitorV2,PerMonitor'
        longPathAware = 'true'
    }
    foreach ($setting in $expectedManifestSettings.Keys) {
        $node = $manifest.SelectSingleNode("//*[local-name()='$setting']")
        $actual = if ($null -eq $node) { '' } else { $node.InnerText.Trim() }
        if (-not [string]::Equals(
                $actual,
                [string]$expectedManifestSettings[$setting],
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Nightlock.exe manifest has incorrect $setting value: '$actual'"
        }
    }

    $executionLevel = $manifest.SelectSingleNode(
        "//*[local-name()='requestedExecutionLevel']")
    if (($null -eq $executionLevel) -or
        ($executionLevel.GetAttribute('level') -ne 'asInvoker') -or
        ($executionLevel.GetAttribute('uiAccess') -ne 'false')) {
        throw 'Nightlock.exe manifest must request asInvoker with uiAccess disabled.'
    }

    $supportedWindows = $manifest.SelectSingleNode(
        "//*[local-name()='supportedOS' and " +
        "@Id='{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}']")
    if ($null -eq $supportedWindows) {
        throw 'Nightlock.exe manifest does not declare Windows 10/11 compatibility.'
    }

    $applicationIdentity = $manifest.SelectSingleNode(
        "/*[local-name()='assembly']/*[local-name()='assemblyIdentity' and " +
        "@name='Nightlock.Nightlock']")
    if (($null -eq $applicationIdentity) -or
        ($applicationIdentity.GetAttribute('version') -ne "$ExpectedVersion.0")) {
        throw 'Nightlock.exe manifest identity does not match the package version.'
    }

    # Keep this explicit dependency through MSVC's manifest merge and the
    # MinGW RT_MANIFEST path to preserve themed controls and file dialogs.
    $commonControls = $manifest.SelectSingleNode(
        "//*[local-name()='assemblyIdentity' and " +
        "@name='Microsoft.Windows.Common-Controls']")
    if (($null -eq $commonControls) -or
        ($commonControls.GetAttribute('version') -ne '6.0.0.0') -or
        ($commonControls.GetAttribute('publicKeyToken') -ne '6595b64144ccf1df')) {
        throw 'Nightlock.exe manifest is missing Common-Controls v6 activation.'
    }

    # RT_GROUP_ICON = 14; ID 101 is deliberately stable in nightlock.rc.in.
    # Validate the actual resource embedded in the PE, not only the source ICO.
    $iconGroup = [Nightlock.PackageAudit.NativeResourceReader]::ReadResource(
        $LiteralPath, 14, 101)
    if ($iconGroup.Length -lt 6) {
        throw 'Nightlock.exe has a truncated group-icon resource.'
    }
    $iconType = [BitConverter]::ToUInt16($iconGroup, 2)
    $iconCount = [BitConverter]::ToUInt16($iconGroup, 4)
    if (($iconType -ne 1) -or
        ($iconCount -eq 0) -or
        ($iconGroup.Length -lt (6 + (14 * $iconCount)))) {
        throw 'Nightlock.exe has an invalid group-icon resource.'
    }

    $iconSizes = [System.Collections.Generic.HashSet[int]]::new()
    for ($index = 0; $index -lt $iconCount; $index++) {
        $offset = 6 + (14 * $index)
        $width = [int]$iconGroup[$offset]
        $height = [int]$iconGroup[$offset + 1]
        if ($width -eq 0) { $width = 256 }
        if ($height -eq 0) { $height = 256 }
        $bitDepth = [BitConverter]::ToUInt16($iconGroup, $offset + 6)
        if (($width -ne $height) -or ($bitDepth -lt 32)) {
            throw "Nightlock.exe contains an invalid $($width)x$height icon frame."
        }
        [void]$iconSizes.Add($width)
    }
    foreach ($requiredSize in @(16, 32, 48, 64, 128, 256)) {
        if (-not $iconSizes.Contains($requiredSize)) {
            throw "Nightlock.exe has no ${requiredSize}x${requiredSize} icon resource."
        }
    }
}

function Assert-InstallerVersionInfo {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LiteralPath,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedVersion
    )

    $versionInfo = (Get-Item -LiteralPath $LiteralPath).VersionInfo
    $expectedFields = @{
        CompanyName = 'Nightlock contributors'
        FileDescription = 'Nightlock installer'
        FileVersion = "$ExpectedVersion.0"
        ProductName = 'Nightlock'
        ProductVersion = $ExpectedVersion
    }
    foreach ($field in $expectedFields.Keys) {
        $actual = ([string]$versionInfo.$field).Trim()
        if (-not [string]::Equals(
                $actual,
                [string]$expectedFields[$field],
                [StringComparison]::Ordinal)) {
            throw "Setup has incorrect $field metadata: '$actual'"
        }
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
        'plugins\platforms\qwindows.dll',
        'plugins\iconengines\qsvgicon.dll',
        'plugins\imageformats\qico.dll',
        'plugins\imageformats\qsvg.dll'
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
    $objects += Get-Item -LiteralPath (Join-Path $Root 'plugins\iconengines\qsvgicon.dll')
    $objects += Get-Item -LiteralPath (Join-Path $Root 'plugins\imageformats\qico.dll')
    $objects += Get-Item -LiteralPath (Join-Path $Root 'plugins\imageformats\qsvg.dll')

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
Assert-NightlockWindowsIdentity `
    -LiteralPath (Join-Path $resolvedStageDir 'Nightlock.exe') `
    -ExpectedVersion $ExpectedVersion

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
Assert-InstallerVersionInfo `
    -LiteralPath $resolvedInstaller `
    -ExpectedVersion $ExpectedVersion

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
Assert-NightlockWindowsIdentity `
    -LiteralPath (Join-Path $installDir 'Nightlock.exe') `
    -ExpectedVersion $ExpectedVersion

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
$dpiAwareness = `
    [Nightlock.PackageAudit.NativeResourceReader]::ReadProcessDpiAwareness(
        $guiProcess.Handle)
if ($dpiAwareness -ne 2) {
    $guiProcess.Kill()
    throw "Installed GUI is not per-monitor DPI aware (value: $dpiAwareness)."
}
if (-not $guiProcess.WaitForExit(30000)) {
    $guiProcess.Kill()
    throw 'Installed GUI did not finish its deterministic smoke run within 30 seconds.'
}
if ($guiProcess.ExitCode -ne 0) {
    throw "Installed GUI exited with code $($guiProcess.ExitCode)."
}
Assert-NonEmptyPng `
    -LiteralPath $screenshotPath `
    -Description 'Installed GUI smoke screenshot'

# Exercise the Windows layered/context-popup path independently of the main
# window screenshot. It catches startup crashes and missing image/icon plugins
# even though final shadow/compositor quality still needs visual QA on Windows.
Remove-Item Env:NIGHTLOCK_SCREENSHOT -ErrorAction SilentlyContinue
Remove-Item Env:NIGHTLOCK_SCREENSHOT_DELAY -ErrorAction SilentlyContinue
$menuScreenshotPath = Join-Path $smokeRoot 'nightlock-context-menu.png'
$env:NIGHTLOCK_SCREENSHOT_MENU = $menuScreenshotPath
$menuProcess = Start-Process `
    -FilePath $guiPath `
    -WorkingDirectory $installDir `
    -PassThru
if (-not $menuProcess.WaitForExit(30000)) {
    $menuProcess.Kill()
    throw 'Installed GUI context-menu smoke run timed out after 30 seconds.'
}
if ($menuProcess.ExitCode -ne 0) {
    throw "Installed GUI context-menu smoke exited with code $($menuProcess.ExitCode)."
}
Assert-NonEmptyPng `
    -LiteralPath $menuScreenshotPath `
    -Description 'Installed GUI context-menu screenshot'

Write-Host "Windows package smoke test passed for Nightlock $ExpectedVersion."
