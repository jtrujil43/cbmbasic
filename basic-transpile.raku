#!/usr/bin/env raku

use v6.d;

sub usage() {
    say "Usage: basic-transpile.raku [--to c64|handbasic] [--output FILE] INPUT";
    say "Use INPUT '-' or omit it to read from standard input.";
}

sub split-statements(Str $source-line --> Array) {
    my @parts;
    my $part = '';
    my $quoted = False;
    my $comment = False;
    my @chars = $source-line.comb;
    for ^@chars.elems -> $index {
        my $char = @chars[$index];
        if $char eq '"' {
            $quoted = !$quoted;
            $part ~= $char;
        } elsif !$quoted && !$comment && $char.lc eq 'r' &&
                $index + 2 < @chars.elems &&
                @chars[$index + 1].lc eq 'e' && @chars[$index + 2].lc eq 'm' &&
                ($index == 0 || @chars[$index - 1] ~~ /<[\s:]>/) {
            $comment = True;
            $part ~= $char;
        } elsif $char eq ':' && !$quoted && !$comment {
            @parts.push($part.trim);
            $part = '';
        } else {
            $part ~= $char;
        }
    }
    @parts.push($part.trim) if $part.trim.chars;
    @parts
}

sub split-arguments(Str $source --> Array) {
    my @parts;
    my $part = '';
    my $quoted = False;
    my $depth = 0;
    for $source.comb -> $char {
        if $char eq '"' {
            $quoted = !$quoted;
            $part ~= $char;
        } elsif !$quoted && $char eq '(' {
            $depth++;
            $part ~= $char;
        } elsif !$quoted && $char eq ')' {
            $depth--;
            $part ~= $char;
        } elsif !$quoted && $depth == 0 && $char eq ',' {
            @parts.push($part.trim);
            $part = '';
        } else {
            $part ~= $char;
        }
    }
    @parts.push($part.trim);
    @parts
}

sub transpile(Str $source, Str $target --> Str) {
    my @source-lines = $source.lines;
    my @records;
    my %used;
    my $next-auto-line = 10;
    my $maximum-line = 0;
    my @identifiers = $source.match(/<[A..Za..z]><[A..Za..z0..9]>*/,:g) // [];
    my %occupied = @identifiers.map({ .substr(0,2).uc => True }).Hash;
    my @candidates;
    for <B H J K Q X Y Z> -> $letter {
        @candidates.append(('0'..'9').map({ $letter ~ $_ }));
    }
    my @names = @candidates.grep({ !%occupied{$_} }).head(33);
    die "Cannot allocate private C64 BASIC scratch variables.\n" if @names.elems < 33;
    my @keys = <active mode old11 old16 old18 old20 old21 old22 sx sy fresh x y color x1 y1 x2 y2 cx cy rx ry fill dx dy steps index address mask byte bit angle scan>;
    my %v = @keys Z=> @names;

    for @source-lines -> $original {
        if $original.trim ~~ /^ (\d+) \s* / {
            $maximum-line = +$0 if +$0 > $maximum-line;
        }
    }
    my $auto-line = $maximum-line + 10;
    for @source-lines -> $original {
        my $trimmed = $original.trim;
        next unless $trimmed.chars || $trimmed.starts-with('#!');
        unless $trimmed ~~ /^\d+/ || $trimmed.starts-with('#!') {
            $maximum-line = $auto-line;
            $auto-line += 10;
        }
    }
    $next-auto-line = $auto-line;
    my $base = $maximum-line + 10;
    while %used{$base} { $base += 10 }
    my %runtime = init => $base, dispatch => $base + 10, exit => $base + 20,
        clear => $base + 30, pixel => $base + 40, line => $base + 50,
        circle => $base + 60;
    die "No room to append graphics routines after line $maximum-line.\n"
        if $base + 74 > 63999;

    for @source-lines -> $original {
        my $line = $original.trim;
        next unless $line.chars;
        next if $line.starts-with('#!');

        my $line-number;
        my $body = $line;
        if $line ~~ /^ (\d+) \s* (.*) $/ {
            $line-number = +$0;
            $body = ~$1;
            die "Line number $line-number is outside the C64 BASIC range (0..63999).\n"
                if $line-number > 63999;
        } else {
            $line-number = $next-auto-line;
        }
        $next-auto-line = $line-number + 10 if $line-number >= $next-auto-line;
        $maximum-line = $line-number if $line-number > $maximum-line;
        %used{$line-number} = True;

        if $target eq 'handbasic' {
            my @c64 = split-statements($body);
            my @translated;
            for @c64 -> $statement {
                if $statement ~~ /^ :i 'SYS' \s+ '1' <|w> $/ {
                    @translated.push('');
                } elsif $statement ~~ /^ :i 'PRINT' \s+ 'CHR$' \s* '(' \s* '147' \s* ')' $/ {
                    @translated.push('SCREEN 0:CLS');
                } elsif $statement ~~ /^ :i 'SYS' \s+ / {
                    note "Warning: line $line-number: $statement calls native C64 code and cannot be translated to HandBASIC.";
                    @translated.push("REM UNSUPPORTED C64 NATIVE CALL: $statement");
                } else {
                    @translated.push($statement);
                }
            }
            my $translated-line = @translated.grep(*.chars).join(':');
            $translated-line ||= 'REM C64 EXTENSION SWITCH OMITTED';
            @records.push([$line-number, $translated-line]);
        } else {
            my @statements = split-statements($body);
            my @converted;
            for @statements -> $statement {
                if $statement ~~ /^ :i 'REM' <|w> / {
                    @converted.push($statement);
                    last;
                }
                if $statement ~~ /^ :i 'SYS' \s+ ('0' | '1') <|w> / {
                    @converted.push('REM CBMBASIC EXTENSION SWITCH OMITTED ON C64');
                    next;
                }
                if $statement ~~ /^ :i 'SCREEN' \s+ (.+) $/ {
                    my $mode = ~$0;
                    @converted.push("{%v<mode>}={$mode}:GOSUB {%runtime<dispatch>}");
                    next;
                }
                if $statement ~~ /^ :i 'CLS' <|w> / {
                    @converted.push("GOSUB {%runtime<clear>}");
                    next;
                }
                if $statement ~~ /^ :i 'PSET' \s+ (.+) $/ {
                    my @args = split-arguments(~$0);
                    die "PSET needs x,y,color at line $line-number\n" unless @args.elems == 3;
                    @converted.push(
                        "{%v<x>}=INT(({@args[0]})*{%v<sx>}):{%v<y>}=INT(({@args[1]})*{%v<sy>})+20:{%v<color>}={@args[2]}:GOSUB {%runtime<pixel>}"
                    );
                    next;
                }
                if $statement ~~ /^ :i 'LINE' \s+ (.+) $/ {
                    my @args = split-arguments(~$0);
                    die "LINE needs x1,y1,x2,y2,color at line $line-number\n" unless @args.elems == 5;
                    @converted.push(
                        "{%v<x1>}=INT(({@args[0]})*{%v<sx>}):{%v<y1>}=INT(({@args[1]})*{%v<sy>})+20:" ~
                        "{%v<x2>}=INT(({@args[2]})*{%v<sx>}):{%v<y2>}=INT(({@args[3]})*{%v<sy>})+20:" ~
                        "{%v<color>}={@args[4]}:GOSUB {%runtime<line>}"
                    );
                    next;
                }
                if $statement ~~ /^ :i 'CIRCLE' \s+ (.+) $/ {
                    my @args = split-arguments(~$0);
                    die "CIRCLE needs x,y,radius[,color[,fill]] at line $line-number\n"
                        unless @args.elems >= 3 && @args.elems <= 5;
                    my $color = @args.elems >= 4 ?? @args[3] !! '1';
                    my $fill = @args.elems == 5 ?? @args[4] !! '0';
                    @converted.push(
                        "{%v<cx>}=INT(({@args[0]})*{%v<sx>}):{%v<cy>}=INT(({@args[1]})*{%v<sy>})+20:" ~
                        "{%v<rx>}=ABS(({@args[2]})*{%v<sx>}):{%v<ry>}=ABS(({@args[2]})*{%v<sy>}):" ~
                        "{%v<color>}={$color}:{%v<fill>}={$fill}:GOSUB {%runtime<circle>}"
                    );
                    next;
                }
                if $statement ~~ /^ :i 'TITLE' <|w> / ||
                   $statement ~~ /^ :i 'CSET' <|w> / ||
                   $statement ~~ /^ :i 'GPRINT' <|w> / ||
                   $statement ~~ /^ :i 'ELLIPSE' <|w> / ||
                   $statement ~~ /^ :i 'ARC' <|w> / ||
                   $statement ~~ /^ :i 'TRIANGLE' <|w> / ||
                   $statement ~~ /^ :i 'FSET' <|w> / ||
                   $statement ~~ /^ :i 'ANTIALIAS' <|w> / ||
                   $statement ~~ /^ :i 'LSET' <|w> / ||
                   $statement ~~ /^ :i 'RECT' <|w> / ||
                   $statement ~~ /^ :i 'BEGINFRAME' <|w> / ||
                   $statement ~~ /^ :i 'ENDFRAME' <|w> / ||
                   $statement ~~ /^ :i 'FRAME' <|w> / {
                    note "Warning: line $line-number: $statement is not representable by the C64 monochrome bitmap target; omitted.";
                    @converted.push('');
                    next;
                }
                if $statement ~~ /^ :i 'END' <|w> / {
                    @converted.push("GOSUB {%runtime<exit>}:END");
                    next;
                }
                if $statement ~~ /^ :i 'SLEEP' <|w> / ||
                   $statement ~~ /^ :i 'ALERT' <|w> / ||
                   $statement ~~ /^ :i 'BEEP' <|w> / {
                    note "Warning: line $line-number: $statement has no direct C64 BASIC V2 equivalent; omitted.";
                    @converted.push('');
                    next;
                }
                @converted.push($statement);
            }
            my $converted-line = @converted.grep(*.chars).join(':');
            $converted-line ||= "REM UNSUPPORTED HANDBASIC GRAPHICS: $body";
            @records.push([$line-number, $converted-line]);
        }
    }

    return @records.sort(*.[0]).map({ "{.[0]} {.[1]}" }).join("\n") ~ "\n"
        if $target eq 'handbasic';

    my @helper;
    sub add-line(Int $offset, Str $statement) {
        @helper.push(($base + $offset) ~ ' ' ~ $statement);
    }

    # Keep one unique variable name for each private value; C64 BASIC only
    # distinguishes the first two characters of variable identifiers.
    add-line(0, "IF {%v<active>}=1 THEN RETURN");
    add-line(1, "{%v<old11>}=PEEK(53265):{%v<old16>}=PEEK(53270):{%v<old18>}=PEEK(53272)");
    add-line(2, "{%v<old20>}=PEEK(53280):{%v<old21>}=PEEK(53281):{%v<old22>}=PEEK(56576)");
    add-line(3, "{%v<active>}=1:POKE 56576,PEEK(56576) AND 252 OR 3");
    add-line(4, "POKE 53272,PEEK(53272) AND 15 OR 24");
    add-line(5, "POKE 53265,PEEK(53265) OR 32:POKE 53270,PEEK(53270) AND 239");
    add-line(6, "POKE 53280,0:POKE 53281,0:GOSUB {%runtime<clear>}:{%v<fresh>}=1");
    add-line(7, "RETURN");
    add-line(10, "IF {%v<mode>}=0 THEN {$base + 12}");
    add-line(11, "{%v<sx>}=.4/{%v<mode>}:{%v<sy>}=.4/{%v<mode>}:GOSUB {%runtime<init>}:RETURN");
    add-line(12, "GOSUB {%runtime<exit>}:RETURN");
    add-line(20, "IF {%v<active>}<>1 THEN RETURN");
    add-line(21, "POKE 53265,{%v<old11>}:POKE 53270,{%v<old16>}:POKE 53272,{%v<old18>}");
    add-line(22, "POKE 53280,{%v<old20>}:POKE 53281,{%v<old21>}:POKE 56576,{%v<old22>}:{%v<active>}=0:RETURN");
    add-line(30, "IF {%v<active>}=1 THEN {$base + 32}");
    add-line(31, "PRINT CHR\$(147):RETURN");
    add-line(32, "IF {%v<fresh>}=0 THEN {$base + 34}");
    add-line(33, "{%v<fresh>}=0:RETURN");
    add-line(34, "FOR {%v<index>}=8192 TO 16191:POKE {%v<index>},0:NEXT");
    add-line(35, "FOR {%v<index>}=0 TO 999:POKE 1024+{%v<index>},16:POKE 55296+{%v<index>},1:NEXT");
    add-line(36, "RETURN");
    add-line(40, "IF {%v<active>}<>1 THEN RETURN");
    add-line(41, "IF {%v<x>}<0 OR {%v<x>}>319 OR {%v<y>}<0 OR {%v<y>}>199 THEN RETURN");
    add-line(42, "{%v<address>}=8192+INT({%v<y>}/8)*320+INT({%v<x>}/8)*8+{%v<y>}-INT({%v<y>}/8)*8");
    add-line(43, "{%v<mask>}=2^(7-({%v<x>}-INT({%v<x>}/8)*8)):{%v<byte>}=PEEK({%v<address>})");
    add-line(44, "{%v<bit>}=INT({%v<byte>}/{%v<mask>})-2*INT({%v<byte>}/(2*{%v<mask>}))");
    add-line(45, "IF {%v<color>}=0 AND {%v<bit>}=1 THEN POKE {%v<address>},{%v<byte>}-{%v<mask>}");
    add-line(46, "IF {%v<color>}<>0 AND {%v<bit>}=0 THEN POKE {%v<address>},{%v<byte>}+{%v<mask>}");
    add-line(47, "RETURN");
    add-line(50, "{%v<dx>}={%v<x2>}-{%v<x1>}:{%v<dy>}={%v<y2>}-{%v<y1>}");
    add-line(51, "{%v<steps>}=ABS({%v<dx>}):IF ABS({%v<dy>})>{%v<steps>} THEN {%v<steps>}=ABS({%v<dy>})");
    add-line(52, "IF {%v<steps>}=0 THEN {%v<x>}={%v<x1>}:{%v<y>}={%v<y1>}:GOSUB {%runtime<pixel>}:RETURN");
    add-line(53, "FOR {%v<index>}=0 TO {%v<steps>}");
    add-line(54, "{%v<x>}=INT({%v<x1>}+{%v<index>}*{%v<dx>}/{%v<steps>}+.5):{%v<y>}=INT({%v<y1>}+{%v<index>}*{%v<dy>}/{%v<steps>}+.5)");
    add-line(55, "GOSUB {%runtime<pixel>}:NEXT:RETURN");
    add-line(60, "IF {%v<fill>}<>0 THEN {$base + 70}");
    add-line(61, "{%v<x1>}={%v<cx>}+{%v<rx>}:{%v<y1>}={%v<cy>}");
    add-line(62, "FOR {%v<angle>}=.0872665 TO 6.37045 STEP .0872665");
    add-line(63, "{%v<x2>}=INT({%v<cx>}+{%v<rx>}*COS({%v<angle>}))");
    add-line(64, "{%v<y2>}=INT({%v<cy>}+{%v<ry>}*SIN({%v<angle>}))");
    add-line(65, "GOSUB {%runtime<line>}:{%v<x1>}={%v<x2>}:{%v<y1>}={%v<y2>}:NEXT:RETURN");
    add-line(70, "IF {%v<ry>}=0 THEN RETURN");
    add-line(71, "FOR {%v<scan>}=-INT({%v<ry>}) TO INT({%v<ry>})");
    add-line(72, "{%v<y1>}={%v<cy>}+{%v<scan>}:{%v<dx>}=INT({%v<rx>}*SQR(1-({%v<scan>}/{%v<ry>})^2))");
    add-line(73, "{%v<x1>}={%v<cx>}-{%v<dx>}:{%v<x2>}={%v<cx>}+{%v<dx>}:{%v<y2>}={%v<y1>}");
    add-line(74, "GOSUB {%runtime<line>}:NEXT:RETURN");

    my @expanded = @records.map({ "{.[0]} {.[1]}" });
    @expanded.append(@helper);
    @expanded.sort({
        $^left.match(/^(\d+)/)[0].Int <=> $^right.match(/^(\d+)/)[0].Int
    }).join("\n") ~ "\n"
}

my $target = 'c64';
my $output;
my $input;
while @*ARGS {
    my $arg = @*ARGS.shift;
    given $arg {
        when '--help' | '-h' { usage(); exit 0 }
        when '--to' {
            die "--to requires c64 or handbasic.\n" unless @*ARGS;
            $target = @*ARGS.shift.lc;
        }
        when /^ '--to=' (.*) $/ { $target = ~$0.lc }
        when '--output' | '-o' {
            die "$arg requires a filename.\n" unless @*ARGS;
            $output = @*ARGS.shift;
        }
        when /^ '--output=' (.*) $/ { $output = ~$0 }
        when { $arg eq '-' } {
            die "Only one input file may be specified.\n" if $input.defined;
            $input = '-';
        }
        when { $arg.starts-with('-') } { die "Unknown option: $arg\n" }
        when * {
            die "Only one input file may be specified.\n" if $input.defined;
            $input = $arg;
        }
    }
}
die "Unknown target '$target'; choose c64 or handbasic.\n" unless $target eq any(<c64 handbasic>);
my $text = !$input.defined || $input eq '-' ?? $*IN.slurp !! $input.IO.slurp;
my $result = transpile($text, $target);
if $output.defined {
    $output.IO.spurt($result);
} else {
    print $result;
}
