use strict;
use warnings;
use Test::More;
use t::Util;
use File::Temp qw(tempfile tempdir);
use Digest::MD5 qw(md5_hex);
use Data::Dumper;

sub run_bench {
    my ($K, $N, $T) = @_;
    return run_prog("./t/00util/bench $K $N $T");
}

subtest "sample benchmark" => sub {
    my $T = 1280;
    my @targets = ("5-2", "7-2", "10-3", "20-5", "50-5", "100-10", "200-20");
    if ($ENV{CI_EMULATION}) {
        @targets = ("5-2", "7-2", "10-3", "20-5");
        $T = 256;
    }
    foreach (@targets) {
      my ($K, $N) = split("-", $_);
      my $resp = run_bench($K, $N, $T);

      my ($enc_mbps) = $resp =~ /encoded.*throughput: ([0-9\.]+)MB/;
      my ($dec_mbps) = $resp =~ /decoded.*throughput: ([0-9\.]+)MB/;

      diag "RS$_ T: $T enc/dec MBps: $enc_mbps/$dec_mbps";
      ok $enc_mbps > 0, "non zero enc mbps RS$_";
      ok $dec_mbps > 0, "non zero dec mbps RS$_";
    }
};
        
done_testing();
