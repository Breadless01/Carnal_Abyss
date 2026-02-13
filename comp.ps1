$ProjectRoot = $PSScriptRoot
$BuildDir = "$ProjectRoot\build"
if (Test-Path "$BuildDir") {
    Remove-Item -Recurse -Force "$BuildDir"
}

$env:PYTHONPATH = "$PSScriptRoot\python";"$PSScriptRoot\.venv\Lib\site-packages"

cmake -S native -B build -DPython3_ROOT_DIR="C:/Users/ich-w/AppData/Local/Programs/Python/Python313"
cmake --build build --config Release
.\build\Release\Game.exe