# cbmbasic - Commodore BASIC V2 as a scripting language

"Commodore BASIC" (cbmbasic) is a 100% compatible version of Commodore's version of Microsoft BASIC 6502 as found on the Commodore 64. You can use it in interactive mode or pass a BASIC file as a command line parameter.

This source does not emulate 6502 code; all code is completely native. On a 1 GHz CPU you get about 1000x speed compared to a 1 MHz 6502.

## Compatibility

It has been tested with

* Mac OS X 10.4/10.5 i386/x86_64/ppc (GCC 3.3/4.0)
* Debian Linux Etch (GCC 3.3/4.1)
* Windows NT (Visual Studio 2005/2008)

Other CPUs, operating systems and compilers should work, too.

Windows users should also install the [Microsoft Visual C++ 2005 SP1 Redistributable Package (x86)]("http://www.microsoft.com/downloads/details.aspx?familyid=200b2fd9-ae1a-4a14-984d-389c36f85647&displaylang=en").

## Usage
You can use cbmbasic in interactive mode by just running the binary without parameters, or you can specify an ASCII-encoded BASIC program on the command line. You can also use cbmbasic as a UNIX scripting language by adding a hashbang line to your BASIC program and making it executable.

    $ ls -l hello.bas 
    -rwxr-xr-x  1 mist  staff  40  7 Apr 21:30 hello.bas
    $ cat hello.bas 
    #!/usr/bin/env cbmbasic
    PRINT"HELLO WORLD!"
    $ ./hello.bas 
    HELLO WORLD!

cbmbasic implements a small plugin system that lets developers add additional statements and functions. It is enabled automatically; use `SYS 0` to disable it and `SYS 1` to re-enable it. The original extensions include LOCATE *y*, *x* (set cursor position), SYSTEM *string* (run command line command), and extended WAIT *port*, *mask*, which implements the [Bill Gates easter egg](http://www.pagetable.com/?p=43).

An optional SDL2 build adds `BEEP frequency,duration` (20-20000 Hz and
1-5000 ms), `FRAME "title","message"` (a modal message box) or
`FRAME x1,y1,x2,y2,color` (a rectangle outline), and graphics
statements `SCREEN`, `CLS`, `TITLE`, `CSET`, `RECT x1,y1,x2,y2,color`,
`PSET`, `BEGINFRAME`,
`ENDFRAME`, `ANTIALIAS`, `LSET`, `FSET`, `LINE`, `CIRCLE`, `ELLIPSE`, `ARC`,
`TRIANGLE`, `GPRINT`, `ALERT`, and `SLEEP`. `SCREEN 1` through `SCREEN 3`
open a pixel canvas; `SCREEN 0` closes it. Logical canvas sizes are 800x400
times the selected mode. The legacy HandBASIC interface remains available:
the 16-bit width is at `1038`/`1039`, height at `1040`/`1041`, selected mode
at `1042`, and latest key or mouse event at `1024`. Matching aliases are
available at `49152`-`49157` without overlapping C64 screen RAM.
`SCREEN 0` closes the extension canvas.

The SDL build also renders common VIC-II memory configurations written through
`POKE`: 40x25 text, 320x200 two-color bitmap, and extended-color text. It reads
screen RAM at `$0400`, color RAM at `$D800`, and VIC-II registers at `$D000`.
For bitmap mode, set `$D011` bit 5 and use `$D018` to select screen and bitmap
bases; for extended-color text, set `$D011` bit 6. If `$D018` is left zero, the
renderer uses the standard `$14` screen/character layout. With no explicit
CIA2 bank selection, it uses VIC bank zero. `test/vic2-graphics.bas` exercises
the supported modes.

Run `test/graphics.bas` as an extension graphics smoke test.

The Raku `basic-transpile.raku` utility translates HandBASIC graphics programs
to C64/VICE bitmap graphics and passes VIC-II memory programs back to HandBASIC.
See [BASIC_TRANSPILER.md](BASIC_TRANSPILER.md) for supported operations and
graphics limitations.

Install SDL2 development files and `pkg-config` first (on Ubuntu/Debian:
`sudo apt install libsdl2-dev pkg-config`), then build with
`make clean && make SDL=1`. The Makefile checks this dependency and prints an
installation hint when it is missing. The default build has no SDL dependency; SDL
statements report an error unless support is enabled. The extension parser is
enabled automatically; `SYS 0` disables it and `SYS 1` re-enables it. For the
legacy Windows makefile, set `SDL2DIR` to the SDL2
development package directory when invoking `nmake`; override `SDL2_LIBDIR`
if the SDL2 import library is not under `lib\x86`. Place `SDL2.dll` beside
the resulting executable.

`test/olivia9.bas` enables the extensions itself. Run it from the test
directory with `cd test && ../cbmbasic olivia9.bas`; `PORTRT10.DAT` is in
that directory.

## Internals
The core of the BASIC interpreter is in the file `cbmbasic.c`, which is platform, endianness and bitness independent. For all I/O, it calls out into `runtime.c`, so it should be possible to adapt this project for any OS by just changing `runtime.c`.

All function calls that the core interpreter can't handle end up in kernal_dispatch() in `runtime.c`, where a switch statement dispatches these to C functions. For Commodore BASIC, `runtime.c` has to support several Commodore KERNAL library routines. Some of them are very important (CHRIN, CHROUT) and some are only used for certain BASIC statements (LOAD, SAVE, OPEN, SETTIM). `runtime.c` does not implement all functions yet.

## License
Feel free to use this project for any purpose, give credit if you like, and send back improvements to the authors, if you like, so that others can benefit from it. See source for license details.

## Contact
[Michael Steil](mailto:mist@c64.org), [James Abbatiello](mailto:abbeyj@gmail.com)
