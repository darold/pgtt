use Test::Simple tests => 9;

use POSIX qw(locale_h);
setlocale(LC_ALL, 'C');

my $DB = 'gtt_testdb';

# initialize the database
`mkdir t/results 2>/dev/null`;

# get the list of test files
my @files = `ls t/sql/*.sql`;
chomp(@files);

# Run all tests and check diff with file in expected directory
foreach my $f (@files)
{
	next if ($f =~ /aprepare.pg.sql|relocation.sql/);
	my $out = $f;
	$out =~ s#^(.*)/sql/(.*)\.sql$#$1/results/$2.out#;
	my $expected = $out;
	$expected =~ s#/results/#/expected/#;
	my $testname = $out;
	$testname =~ s#.*/(.*)\.out$#$1#;
	`LC_ALL=C psql -f t/sql/aprepare.pg.sql`;
	my $ret = `LC_ALL=C psql -e -d $DB -f $f 2>&1 | grep -v "You are now connected to" > $out`;
	$ret = `diff $out $expected`;
	ok(length($ret) == 0 , "test $testname");
}

