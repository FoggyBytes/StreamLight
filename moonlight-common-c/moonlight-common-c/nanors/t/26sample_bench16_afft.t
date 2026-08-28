use strict;
use warnings;
use Test::More;
use t::Util;

sub run_bench {
    my ($K, $N, $T) = @_;
    my ($stderr, $stdout) = run_prog("./t/00util/bench16_afft $K $N $T");
    return $stdout;
}

subtest "sample benchmark 16 afft" => sub {
    my $T = 1280;
    my @targets = ("5-2", "7-2", "10-3", "20-5", "50-5", "100-10", "200-20", "500-25", "500-50", "1000-50", "1000-100", "2000-100", "2000-200", "5000-250", "5000-500", "10000-500", "10000-1000");
    if ($ENV{CI_EMULATION}) {
        @targets = ("5-2", "7-2", "10-3", "20-5");
        $T = 256;
    }
    my @failures;
    my $summary = "";
    foreach (@targets) {
      my ($K, $N) = split("-", $_);
      my $resp = run_bench($K, $N, $T);

      my ($enc_mbps) = $resp =~ /encoded.*throughput: ([0-9\.]+)MB/;
      my ($dec_mbps) = $resp =~ /decoded.*throughput: ([0-9\.]+)MB/;

      if ($enc_mbps && $dec_mbps && $enc_mbps > 0 && $dec_mbps > 0) {
          $summary .= sprintf("  RS16_AFFT_%-12s T: %d enc/dec MBps: %8.1f / %-8.1f\n", $_, $T, $enc_mbps, $dec_mbps);
      } else {
          push @failures, $_;
      }
    }
    diag "Benchmark Summary:\n" . $summary if $summary;
    ok(!@failures, "all sample benchmarks succeeded with non-zero throughput") or diag "failures: @failures";
};

done_testing();
