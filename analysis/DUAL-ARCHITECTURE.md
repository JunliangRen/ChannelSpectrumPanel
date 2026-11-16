# Dual-architecture builds (0.5.0)

## Scope

The component now targets foobar2000 2.x with matching x86 or x64 Columns UI. Both builds share the C++ engine, renderer, settings, panel GUID, and fixed-width configuration format. Original old/new FFT modes remain available. No old-host compatibility layer was added.

## Implementation

- Plugin and test projects support Release/Win32 and Release/x64. Architecture-specific directories prevent object, executable, log, and preview collisions. SDK project references already support both platforms; shared-Win32.lib and shared-x64.lib are selected by platform.
- mapping_x87_x86.asm implements the cdecl ABI, preserves EBX/ESI/EDI, and uses the same x87 arithmetic and double cache boundaries as the x64 helper. Keep the arithmetic sequences synchronized when changing either helper.
- The x86 helper uses FISTP with truncate rounding for its final conversions instead of requiring SSE3 FISTTP. It restores the caller's x87 control word. SafeSEH remains enabled. This does not establish support for older CPUs across all SDK/runtime code.
- Existing x64 DLL naming is preserved. The new DLL is foo_uie_channel_spectrum_x86.dll; filename validation is selected at compile time.
- build.ps1 defaults to both architectures and runs all tests before packaging. Single-target builds use -Architecture x86 or x64. package.ps1 validates the PE machine type before writing a package. x86 DLLs are at archive root; x64 DLLs are under x64/. Each package has its own SHA-256 file.

## Validation

Both Release builds pass 9,104 checks per invocation, across three styles per architecture. Each run compares 17,637 original coordinate descriptors across 13 cases with zero differences. New tests call the mapping helper repeatedly under four rounding modes and verify output invariance and floating-point control-state restoration. The three synthetic BMP previews match byte for byte between architectures.

These are offline tests. Neither architecture has completed player integration tests. Installation, panel creation, playback, settings, and stability in both portable players remain pending until the maintainer is available. Existing audio fixtures and the host test plan are retained.
