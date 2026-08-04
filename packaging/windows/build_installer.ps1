[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$StageDir,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$AppVersion
)

$ErrorActionPreference = 'Stop'

$programFilesX86 = [Environment]::GetFolderPath(
    [Environment+SpecialFolder]::ProgramFilesX86
)
if ([string]::IsNullOrWhiteSpace($programFilesX86)) {
    throw 'Unable to resolve the 32-bit Program Files directory.'
}

$innoCompiler = Join-Path $programFilesX86 'Inno Setup 6\ISCC.exe'
if (-not (Test-Path -LiteralPath $innoCompiler)) {
    throw "Inno Setup compiler not found at: $innoCompiler"
}

$resolvedStageDir = (Resolve-Path -LiteralPath $StageDir).Path
$installerScript = Join-Path $PSScriptRoot 'nightlock.iss'

& $innoCompiler `
    "/DStageDir=$resolvedStageDir" `
    "/DAppVersion=$AppVersion" `
    $installerScript

if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup failed with exit code $LASTEXITCODE."
}
