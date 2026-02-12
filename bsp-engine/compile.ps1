$ProjectRoot = $PSScriptRoot
$BuildDir = "$ProjectRoot\build"
if (Test-Path "$BuildDir") {
    Remove-Item -Recurse -Force "$BuildDir"
}
cmake -S native -B build
cmake --build build --config Debug
.\build\Debug\Game.exe
