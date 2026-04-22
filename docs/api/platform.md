# platform.h

Portability and visibility macros used throughout the MXL public API.

## Macros

### MXL_EXPORT
Marks a public API symbol for export from shared libraries.

### MXL_NODISCARD
Indicates return values should not be ignored (C++ only).

### MXL_CONSTEXPR
Maps to `constexpr` in C++ or `inline` in C.
