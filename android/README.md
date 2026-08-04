# Umbrly Android Sandbox

Android Studio project for running Umbrly scripts on Android.

## What is included

- Home screen styled after the sketch: header, two starter script tiles, and a create/load tile.
- Editor screen with code, input lines for `INPUT:`, run/save actions, and captured `PRINT:` output.
- JNI bridge that runs the existing C++ lexer, parser, AST interpreter, and core builtins.

## Build

Open the `android/` folder in Android Studio and run the `app` configuration.

On this machine, `gradlew.bat` is wired to:

- Gradle: `C:\Users\Ramix\.gradle\wrapper\dists\gradle-9.5.1-bin\iq79hdu3mqx29lgffhp8bfmx\gradle-9.5.1`
- JDK 17: `C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot`
- CMake: `C:\msys64\mingw64\bin\cmake.exe`

From the `android/` folder you can run:

```bat
gradlew.bat assembleDebug
```

The mobile native library intentionally excludes Windows-only files:

- `src/main.cpp`
- `src/compiler.cpp`
- `src/winapi_bindings.cpp`

That means normal language features and core builtins work, while WinAPI builtins and CLI-only build/compile modes are not part of the Android sandbox.
