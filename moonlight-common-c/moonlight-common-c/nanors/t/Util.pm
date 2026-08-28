package t::Util;

use strict;
use warnings;
use Digest::MD5 qw(md5_hex);
use File::Temp qw(tempfile tempdir);
use Test::More;
use Time::HiRes qw(sleep gettimeofday tv_interval);

use base qw(Exporter);
our @EXPORT = qw(
    ASSETS_DIR
    bindir
    slurp_file
    md5_file
    run_prog
);

use constant ASSETS_DIR => 't/assets';

sub bindir {
    $ENV{BINARY_DIR} || '.';
}

sub slurp_file {
    my $fn = shift;
    open my $fh, "<", $fn
        or die "failed to open file:$fn:$!";
    local $/;
    return (join '', <$fh>);
}

sub md5_file {
    return md5_hex(slurp_file(@_));
}

sub run_prog {
    my $cmd = shift;
    if ($ENV{RUN}) {
        $cmd = "$ENV{RUN} $cmd";
    }
    my ($tempfh, $tempfn) = tempfile(UNLINK => 1);
    my $stderr = `$cmd 2>&1 > $tempfn`;
    my $stdout = do { local $/; <$tempfh> };
    close $tempfh; # tempfile does not close the file automatically (see perldoc)
    return ($stderr, $stdout);
}

1;
