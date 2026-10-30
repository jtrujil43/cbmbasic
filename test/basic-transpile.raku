#!/usr/bin/env raku

use v6.d;
use Test;

my $transpiler = $*PROGRAM.parent.parent.add('basic-transpile.raku');

sub translate(Str $source, Str $target = 'c64') {
    my $process = run 'rakupp', '-q', $transpiler.Str, '--to', $target, '-',
        :in, :out, :err;
    $process.in.print($source);
    $process.in.close;
    my $output = $process.out.slurp;
    my $errors = $process.err.slurp;
    ($output, $errors, $process.exitcode)
}

my ($c64, $warnings, $status) = translate(q:to/BASIC/);
10 SYS 1
20 SCREEN 2:CLS
30 PSET 400,200,2
40 LINE 0,0,800,400,1
50 CIRCLE 400,200,100,1,1
60 GPRINT "A:B",10,10,1
70 END
BASIC

is $status, 0, 'HandBASIC graphics input transpiles';
like $c64, /'POKE 53272'/, 'selects the VIC-II bitmap layout';
like $c64, /'8192 TO 16191'/, 'clears the 320x200 bitmap memory';
unlike $c64, /'PSET' | 'LINE ' | 'CIRCLE '/, 'does not leave source graphics commands untranslated';
like $c64, /'SQR'/, 'emits the filled-circle scanline routine';
like $c64, /'SIN'/, 'emits the outline-circle routine';
unlike $c64, /'SCREEN 2'/, 'does not retain HandBASIC SCREEN syntax';
ok $warnings.contains('GPRINT "A:B",10,10,1 is not representable'),
    'reports graphics text that C64 bitmap mode cannot reproduce';
ok $c64.contains('REM UNSUPPORTED HANDBASIC GRAPHICS: GPRINT "A:B"'),
    'keeps unsupported graphics visible as a BASIC comment';
ok $c64.contains('PRINT CHR$(147)'), 'preserves valid CHR$ syntax in generated BASIC';

my ($hand, $reverse-warnings, $reverse-status) = translate(q:to/BASIC/, 'handbasic');
10 SYS 1:PRINT CHR$(147)
20 POKE 53265,PEEK(53265) OR 32
30 POKE 8192,255
BASIC

is $reverse-status, 0, 'C64 BASIC input transpiles to HandBASIC';
ok $hand.contains('SCREEN 0:CLS'), 'maps the C64 clear-screen character to HandBASIC';
ok $hand.contains('POKE 53265,PEEK(53265) OR 32'),
    'preserves VIC-II register programming for cbmbasic’s VIC renderer';
ok $hand.contains('POKE 8192,255'), 'preserves bitmap-memory writes';
ok !$hand.contains('SYS 1'), 'omits the cbmbasic-only extension hook';
is $reverse-warnings, '', 'does not warn for supported C64-to-HandBASIC patterns';

my ($comments) = translate(q:to/BASIC/);
10 PRINT "colon: stays":REM comment: stays together
20 PSET 0,0,1
BASIC
ok $comments.contains('REM comment: stays together'),
    'does not split BASIC comments on embedded colons';

done-testing;
