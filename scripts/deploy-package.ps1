# Lays the package out as Visual Studio does on F5, and registers it, so
# that the application can be run without Visual Studio: every file the
# build of package\Package.wapproj put in the package, copied to where
# the package has it, under package\bin\<Platform>\<Configuration>\AppX.
#
#   msbuild package\Package.wapproj /restore /p:Configuration=Debug /p:Platform=x64
#   scripts\deploy-package.ps1 [-Configuration Debug] [-Platform x64]
#
# A package registered from somewhere else under the same name is taken
# away first: Windows keeps one of a name, and will not register a
# second from another place. Then start it from the Start menu, or:
#
#   explorer.exe shell:AppsFolder\<family name>!App

param(
    [string] $Configuration = 'Debug',
    [string] $Platform = 'x64'
)

$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $here "package\bin\$Platform\$Configuration"
$recipe = Join-Path $bin 'package.build.appxrecipe'
$layout = Join-Path $bin 'AppX'
if (-not (Test-Path $recipe)) {
    throw "no build to lay out: $recipe (build package\Package.wapproj first)"
}

# What the build decided goes in the package, and where.
$xml = [xml](Get-Content $recipe -Raw)
$ns = @{ m = 'http://schemas.microsoft.com/developer/msbuild/2003' }
$files = Select-Xml -Xml $xml -XPath '//m:AppxPackagedFile' -Namespace $ns | ForEach-Object { $_.Node }

New-Item -ItemType Directory -Force $layout | Out-Null
foreach ($file in $files) {
    $target = Join-Path $layout $file.PackagePath
    New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
    # Only what changed: a layout of thousands of files is otherwise
    # slow to lay out again.
    if (-not (Test-Path $target) -or
        (Get-Item $file.Include).LastWriteTimeUtc -gt (Get-Item $target).LastWriteTimeUtc) {
        Copy-Item $file.Include $target -Force
    }
}
Copy-Item (Join-Path $bin 'AppxManifest.xml') (Join-Path $layout 'AppxManifest.xml') -Force

$manifest = [xml](Get-Content (Join-Path $layout 'AppxManifest.xml') -Raw)
$name = $manifest.Package.Identity.Name
$installed = Get-AppxPackage -Name $name
if ($installed -and $installed.InstallLocation -ne $layout) {
    Remove-AppxPackage $installed.PackageFullName
}
# Windows also refuses one registered from here before with a manifest
# that has changed since, as Visual Studio's deploy leaves it: that one
# is taken away as well, and this one registered in its place.
try {
    Add-AppxPackage -Register (Join-Path $layout 'AppxManifest.xml') -ForceUpdateFromAnyVersion
} catch {
    $installed = Get-AppxPackage -Name $name
    if (-not $installed) { throw }
    Remove-AppxPackage $installed.PackageFullName
    Add-AppxPackage -Register (Join-Path $layout 'AppxManifest.xml')
}

$package = Get-AppxPackage -Name $name
"$($files.Count) files laid out in $layout"
"registered $($package.PackageFamilyName) from $($package.InstallLocation)"
