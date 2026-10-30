# HandBASIC / C64 BASIC transpiler

`basic-transpile.raku` converts numbered BASIC listings in either direction.
It is a source-to-source translator, not an interpreter or a 6502 emulator.
It uses Raku and runs with either `raku` or `rakupp`:

```sh
rakupp -q basic-transpile.raku --to c64 input.bas -o output.bas
rakupp -q basic-transpile.raku --to handbasic c64-program.bas -o handbasic.bas
```

The default target is `c64`; `-` or no input filename reads standard input.
Warnings go to standard error. The implementation is informed by the
grammar-driven organization of the TrueBASIC7 project, but deliberately has no
dependency on that interpreter.

## HandBASIC to C64/VICE

The translator generates pure BASIC V2 VIC-II high-resolution bitmap setup and
drawing subroutines. It translates:

| HandBASIC | C64/VICE output |
|---|---|
| `SCREEN n` / `SCREEN 0` | Enter/leave VIC-II bitmap mode; save and restore the VIC-II/CIA2 registers |
| `CLS` | Clear the active bitmap or clear the text screen |
| `PSET x,y,color` | Set or clear one bitmap pixel |
| `LINE x1,y1,x2,y2,color` | Draw a clipped line using BASIC pixel plotting |
| `CIRCLE x,y,r,color,fill` | Draw a circle or filled disk |
| `END` | Restore the previous display registers before ending |

Coordinates are scaled into the 320×200 VIC-II bitmap and letterboxed vertically
to preserve the HandBASIC 2:1 canvas aspect ratio. The bitmap is monochrome:
color zero clears a pixel and any nonzero color sets it. Drawing uses BASIC
loops and is substantially slower than HandBASIC’s native graphics extension.

Text graphics (`GPRINT`), custom palettes, titles, rectangles, ellipses, arcs,
triangles, font/line-width/antialias controls, frame controls, sound, and timing
extensions have no equivalent in this target. Such statements are omitted with
a warning; an otherwise-empty line is retained as a `REM` diagnostic in the
generated listing. Their absence is not silently treated as a successful
conversion. Other BASIC statements are passed through as-is. The cbmbasic-only
`SYS 1` extension hook is removed.

## C64/VICE to HandBASIC

Most C64 BASIC V2 statements are preserved. `PRINT CHR$(147)` becomes
`SCREEN 0:CLS`, and cbmbasic’s `SYS 1` extension hook is omitted. VIC-II
`POKE` operations and bitmap/screen-memory writes remain intact, allowing
programs using the VIC-II configurations supported by cbmbasic’s SDL renderer
to retain their graphics behavior. Other `SYS` calls invoke native 6502 code and
cannot be translated; the translator warns and comments them out.

This direction does not attempt to decompile arbitrary machine code or infer
graphics primitives from arbitrary `POKE` sequences.

## Testing

Run the Raku translation tests with:

```sh
rakupp -q test/basic-transpile.raku
```
