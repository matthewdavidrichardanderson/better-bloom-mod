param(
    [Parameter(Mandatory = $true)]
    [string]$DusklightDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sdkRoot = (Resolve-Path -LiteralPath $DusklightDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot 'sdk/CMakeLists.txt'))) {
    throw 'DusklightDir must point to a Dusklight checkout containing the mod SDK.'
}
Get-Command cl.exe -ErrorAction Stop | Out-Null

$cmakeBinDir = & python -c 'import cmake; print(cmake.CMAKE_BIN_DIR)'
if ($LASTEXITCODE -ne 0) { throw 'The CMake Python package is unavailable.' }
$ninjaBinDir = & python -c 'import ninja; print(ninja.BIN_DIR)'
if ($LASTEXITCODE -ne 0) { throw 'The Ninja Python package is unavailable.' }
$cmake = Join-Path $cmakeBinDir 'cmake.exe'
$ninja = Join-Path $ninjaBinDir 'ninja.exe'
& $cmake --version
if ($LASTEXITCODE -ne 0) { throw 'CMake could not start.' }
& $ninja --version
if ($LASTEXITCODE -ne 0) { throw 'Ninja could not start.' }

$buildRoot = Join-Path $repoRoot 'build'
& $cmake -S $repoRoot -B $buildRoot -G Ninja `
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$ninja" "-DDUSKLIGHT_DIR=$sdkRoot"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $buildRoot --target better_bloom_package --parallel
if ($LASTEXITCODE -ne 0) { throw 'The Windows package build failed.' }

$package = Join-Path $buildRoot 'mods/better_bloom.dusk'
$archive = [System.IO.Compression.ZipFile]::OpenRead($package)
try {
    foreach ($name in @('mod.json', 'lib/windows-amd64/mod.dll', 'res/filter.wgsl',
                        'res/composite.wgsl', 'res/LICENSE.txt')) {
        $entry = $archive.GetEntry($name)
        if ($null -eq $entry -or $entry.Length -eq 0) {
            throw "The package is missing a required file: $name"
        }
    }
    $reader = [System.IO.StreamReader]::new($archive.GetEntry('mod.json').Open())
    try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    $expected = Get-Content -LiteralPath (Join-Path $repoRoot 'mod.json') -Raw | ConvertFrom-Json
    if ($manifest.id -ne $expected.id -or $manifest.version -ne $expected.version) {
        throw 'The package manifest does not match the source manifest.'
    }
    $reader = [System.IO.StreamReader]::new($archive.GetEntry('res/LICENSE.txt').Open())
    try { $license = $reader.ReadToEnd() } finally { $reader.Dispose() }
    $expectedLicense = Get-Content -LiteralPath (Join-Path $repoRoot 'LICENSE') -Raw
    if ($license.Replace("`r`n", "`n") -ne $expectedLicense.Replace("`r`n", "`n")) {
        throw 'The packaged license does not match LICENSE.'
    }
} finally {
    $archive.Dispose()
}

$outputRoot = Join-Path $repoRoot 'dist'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$outputPackage = Join-Path $outputRoot 'Better Bloom.dusk'
Copy-Item -LiteralPath $package -Destination $outputPackage -Force
$packageHash = (Get-FileHash -LiteralPath $outputPackage -Algorithm SHA256).Hash.ToLowerInvariant()
"$packageHash *Better Bloom.dusk" | Set-Content -LiteralPath "$outputPackage.sha256" -Encoding ascii
Write-Output "Validated Windows package: $outputPackage"
