use strict;
use warnings;
use Test::More;
use t::Util;

sub run_test {
    my $seed = shift;
    my ($stderr, $stdout) = run_prog("./t/00util/test16_afft $seed");
    return $stdout;
}

subtest "robust test battery 16_afft" => sub {
    # Run the robust C unit test battery with 5 different deterministic seeds
    for (my $seed = 100; $seed < 105; $seed++) {
        my $resp = run_test($seed);
        like $resp, "/===OK===/", "Battery sweep with seed $seed";
    }
};

done_testing();
