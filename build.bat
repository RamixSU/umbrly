@echo off
rem Umbrly build script (include/ + src/ modular layout).
rem Kept pure ASCII on purpose: cmd.exe misparses non-ASCII bytes under
rem non-UTF-8 codepages, which can break REM/echo lines mid-script.
setlocal

set GPP=g++
where g++ >nul 2>nul
if errorlevel 1 (
    if exist "C:\msys64\mingw64\bin\g++.exe" (
        set "GPP=C:\msys64\mingw64\bin\g++.exe"
        rem cc1plus.exe looks for its DLLs in mingw64\bin - without this on
        rem PATH the compiler fails silently with no error output.
        set "PATH=C:\msys64\mingw64\bin;%PATH%"
    ) else (
        echo g++ not found. Install MinGW-w64 or MSYS2.
        exit /b 1
    )
)

echo Compiling Umbrly (lexer, parser, ast, interpreter, builtins, winapi_bindings, compiler, main) ...
rem -O3: aggressive optimization. -flto: cross-file inlining across all the .cpp
rem below (Value/eval/exec calls span translation units). -DNDEBUG: drop asserts.
rem NOTE: umbrly.exe's own -b/-c flags need this same src\ and include\ tree at
rem run time (it invokes g++ against them to build compiled programs) - keep
rem umbrly.exe next to include\ and src\, don't move just the .exe elsewhere.
"%GPP%" -std=c++17 -O3 -flto -DNDEBUG -static -s -Iinclude ^
    src\lexer.cpp src\parser.cpp src\ast.cpp src\interpreter.cpp ^
    src\builtins.cpp src\winapi_bindings.cpp src\compiler.cpp src\main.cpp ^
    -o umbrly.exe ^
    -luser32 -lshell32 -lwinmm -ladvapi32
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo Done: umbrly.exe
echo Try it:  umbrly.exe examples\hello.umb
