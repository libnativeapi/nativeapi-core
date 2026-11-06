$ErrorActionPreference = 'Stop'
$build = 'build-capture'
cmake -S . -B $build -DCMAKE_CXX_FLAGS=/utf-8
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmd /c "cmake --build $build --config Debug --target screen_capture_test screen_capture_windows_smoke -- /nologo /v:minimal > $build\capture-build.log 2>&1"
$result = $LASTEXITCODE
Get-Content "$build\capture-build.log" | Select-String 'error |fatal |vcxproj ->|PASS|FAIL'
if ($result -ne 0) { exit $result }
& "$build\tests\Debug\screen_capture_test.exe"
exit $LASTEXITCODE
