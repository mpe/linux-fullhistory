#!/usr/bin/perl

($ME = $0) =~ s|.*/||;

$file = "maps/mac.map";
$line = 1;
open(PC, $file) || die("$!");
while(<PC>)
{
    if(/^\s*keycode\s+(\d+|0x[0-9a-fA-F]+)\s*=\s*(\S+)/)
    {
        my($idx) = $1;
        my($sym) = $2;
        if ($idx =~ "0x.*") {
            $idx = hex($idx);
        } else {
            $idx = int($idx);
        }
        if(defined($map{uc($sym)}))
        {
            # print STDERR "$file:$line: warning: `$sym' redefined\n";
        }
        $map{uc($sym)} = $idx;
    }
    $line++;
}
close(PC);

# $file = "maps/fixup.map";
# $line = 1;
# open(FIXUP, $file) || die("$!");
# while(<FIXUP>)
# {
#     if(/^\s*keycode\s+(\d+)\s*=\s*/)
#     {
#       my($idx) = int($1);
#       for $sym (split(/\s+/, $'))
#         {
#           $map{uc($sym)} = $idx;
#       }
#     }
#     $line++;
# }
# close(FIXUP);

$file = "maps/usb.map";
$line = 1;
open(USB, $file) || die("$!");
while(<USB>)
{
    if(/^\s*keycode\s+(\d+)\s*=\s*/)
    {
        my($idx) = int($1);
        for $sym (split(/\s+/, $'))
        {
            my($val) = $map{uc($sym)};
            $map[$idx] = $val;
            if(!defined($val))
            {
                print STDERR "$file:$line: warning: `$sym' undefined\n";
            }
            else
            {
                last;
            }
        }
    }
    $line++;
}
close(USB);

print "unsigned char usb_kbd_map[256] = \n{\n";
for($x = 0; $x < 32; $x++)
{
    if($x && !($x % 2))
    {
        print "\n";
    }
    print "  ";
    for($y = 0; $y < 8; $y++)
    {
        my($idx) = $x * 8 + $y;
        print sprintf("  0x%02x,",
                      int(defined($map[$idx]) ? $map[$idx]:0));
    }
    print "\n";
}
print "};\n";
