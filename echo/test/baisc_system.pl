
use strict;
my $bindir = shift(@ARGV);
my $echoserver = $bindir . '/echoserver';
my $echoclient = $bindir . '/echoclient';
my $pid = fork();
if ($pid == 0) {
    exit(system($echoserver));
}

my @strs = ( "a", "b b", "c c c", "d d d ");
my @errors = ();
for my $str(@strs) {
    my $command = "$echoclient -m \"$str\"";
    my $result = `$command`;
    push @errors, "failed to run $command" if $?;
    push @errors, "$str does not equal $result after running $command" if $str ne $result;
}
kill('TERM', $pid);
waitpid($pid, 0);
map { print $_ . "\n" } @errors;
exit(@errors);

