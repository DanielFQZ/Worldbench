param(
    [string]$QtRoot = 'E:\AppProject\.worldbench-tools\Qt\6.8.3\msvc2022_64',
    [string]$DockSource = '',
    [string]$Version = '0.26.4-pre.8',
    [switch]$Clean,
    [switch]$SkipTests,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
$root = Split-Path $PSScriptRoot -Parent
$sourceCommit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE) { throw 'Cannot identify source commit.' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio with Desktop development with C++.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC C++ build tools were not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
# Import the environment produced by the trusted Visual Studio bootstrap script.
$bootstrap = 'call "' + $vcvars + '" >nul && set'
& $env:ComSpec /d /c $bootstrap | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        # Git's inherited command-line config may contain meaningful empty values.
        # SetEnvironmentVariable(name, '') deletes them in Windows PowerShell 5.1.
        # The VS bootstrap does not own these variables; leave them untouched.
        if ($matches[1] -notlike 'GIT_CONFIG_*') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path -LiteralPath $ninja)) { $ninja = (Get-Command ninja -ErrorAction Stop).Source }
if (-not (Test-Path -LiteralPath "$QtRoot\bin\qsb.exe")) { throw 'Qt 6.8.3 MSVC x64 with ShaderTools is required.' }
$build = Join-Path $root 'build'
$argsList = @('-S', $root, '-B', $build, '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja", '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG', "-DCMAKE_PREFIX_PATH=$QtRoot", "-DWORLDBENCH_APP_VERSION=$Version")
if ($DockSource) { $argsList += "-DFETCHCONTENT_SOURCE_DIR_KDDOCKWIDGETS=$DockSource" }
& $cmake @argsList
if ($LASTEXITCODE) { throw 'CMake configure failed.' }
$buildArgs = @('--build', $build, '--parallel', '8')
if ($Clean) { $buildArgs += '--clean-first' }
& $cmake @buildArgs
if ($LASTEXITCODE) { throw 'Build failed.' }
$env:PATH = "$QtRoot\bin;$env:PATH"
$testReport = Join-Path $build 'release-tests.xml'
$testsPassed = $false
if (-not $SkipTests) {
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $build --output-on-failure --output-junit $testReport
    if ($LASTEXITCODE) { throw 'Release regression tests failed.' }
    $testsPassed = $true
}
if ($Package) {
    if (-not $testsPassed) { throw 'Distributable builds require the full test suite; -SkipTests cannot be combined with -Package.' }
    $currentCommit = (& git -C $root rev-parse HEAD).Trim()
    $sourceChanges = & git -C $root status --porcelain --untracked-files=all
    if ($LASTEXITCODE -or $sourceChanges -or $currentCommit -ne $sourceCommit) { throw 'Commit all source and documentation changes before creating a distributable build.' }
    $dist = Join-Path $root ("dist\Worldbench-" + $Version)
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    Copy-Item -LiteralPath "$build\Worldbench.exe" -Destination $dist -Force
    Copy-Item -LiteralPath "$build\WorldbenchMcp.exe" -Destination $dist -Force
    & "$QtRoot\bin\windeployqt.exe" --release --qmldir "$root\app\qml" --no-translations --no-opengl-sw "$dist\Worldbench.exe"
    if ($LASTEXITCODE) { throw 'Qt deployment failed.' }
    # The MCP helper is a second executable in the package and is not pulled
    # in by windeployqt when only the GUI target is scanned.
    & "$QtRoot\bin\windeployqt.exe" --release --no-translations --no-opengl-sw "$dist\WorldbenchMcp.exe"
    if ($LASTEXITCODE) { throw 'MCP Qt deployment failed.' }
    Copy-Item -LiteralPath "$root\licenses" -Destination $dist -Recurse -Force
    Copy-Item -LiteralPath "$root\examples" -Destination $dist -Recurse -Force
    $exeHash = (Get-FileHash -LiteralPath "$dist\Worldbench.exe" -Algorithm SHA256).Hash.ToLowerInvariant()
    $mcpHash = (Get-FileHash -LiteralPath "$dist\WorldbenchMcp.exe" -Algorithm SHA256).Hash.ToLowerInvariant()
    [xml]$testXml = Get-Content -LiteralPath $testReport -Raw
    $testCount = $testXml.SelectNodes('//testcase').Count
    # Ship outcomes, not test logs containing local workstation paths/hostnames.
    foreach ($node in @($testXml.SelectNodes('//system-out | //system-err | //properties'))) { [void]$node.ParentNode.RemoveChild($node) }
    foreach ($suite in @($testXml.SelectNodes('//testsuite'))) { $suite.RemoveAttribute('hostname'); $suite.SetAttribute('name', 'Worldbench') }
    foreach ($case in @($testXml.SelectNodes('//testcase'))) { $case.RemoveAttribute('classname') }
    $packagedReport = Join-Path $dist 'release-tests.xml'
    $testXml.Save($packagedReport)
    $testHash = (Get-FileHash -LiteralPath $packagedReport -Algorithm SHA256).Hash.ToLowerInvariant()
    @{version=$Version; date=(Get-Date -Format 'yyyy-MM-dd'); executable_sha256=$exeHash; mcp_sha256=$mcpHash;
      source_commit=$sourceCommit; source_clean=$true; tests_passed=$testsPassed; test_count=$testCount;
      test_report_sha256=$testHash; test_document='TESTING.md'; development_document='DEVELOPMENT.md'} |
        ConvertTo-Json | Set-Content -LiteralPath "$dist\build-info.json" -Encoding UTF8
    & python (Join-Path $PSScriptRoot 'verify-build.py') $root $Version
    if ($LASTEXITCODE) { throw 'Distributable build validation failed.' }
    Write-Host "Runnable test build: $dist\Worldbench.exe"
}
