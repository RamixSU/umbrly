// swift-tools-version: 6.2
import PackageDescription

// iOS build of the Umbrly interpreter.
//
// The C++ target compiles the sources in src/ in place rather than copying
// them, so the app can never drift from the interpreter the CLI and the Android
// sandbox use. Three files stay out, for the same reason they are excluded on
// Android:
//
//   src/main.cpp            — CLI entry point, includes <windows.h>
//   src/compiler.cpp        — emits Windows C++ for native compilation
//   src/winapi_bindings.cpp — Win32 calls; ios/bridge/umbrly_ios.cpp replaces it
let package = Package(
    name: "UmbrlyPad",
    platforms: [.iOS(.v26)],
    products: [
        .library(name: "UmbrlyPad", targets: ["UmbrlyPad"])
    ],
    targets: [
        .target(
            name: "UmbrlyCore",
            path: ".",
            sources: [
                "src/lexer.cpp",
                "src/parser.cpp",
                "src/ast.cpp",
                "src/interpreter.cpp",
                "src/builtins.cpp",
                "ios/bridge/umbrly_ios.cpp"
            ],
            publicHeadersPath: "ios/bridge/include",
            cxxSettings: [
                .headerSearchPath("include")
            ],
            linkerSettings: [
                // The interpreter is C++; Swift targets do not pull libc++ in
                // on their own.
                .linkedLibrary("c++")
            ]
        ),
        .target(
            name: "UmbrlyPad",
            dependencies: ["UmbrlyCore"],
            path: "ios/App"
        )
    ],
    cxxLanguageStandard: .cxx17
)
