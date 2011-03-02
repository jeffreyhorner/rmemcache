#!/usr/bin/perl

$RSRC = shift;
$memcached = shift;
$Rbin = shift;

print "Usage: ./configure.pl /path/to/R/SRC /path/to/memcached /path/to/R \n\n" unless (defined $RSRC and defined $memcached);

open(IN,"<Makefile.in") or die $!;
open (OUT,"+>Makefile") or die $!;
while(<IN>){
	s|\@RSRC\@|$RSRC|g;
	s|\@R\@|$Rbin|g;
	s|\@MEMCACHED\@|$memcached|g;
	print OUT;
}
close(IN);
close(OUT);

open(IN,"<.gdbinit.in") or die $!;
open(OUT,"+>.gdbinit") or die $!;
while(<IN>){
	s|\@RSRC\@|$RSRC|g;
	s|\@R\@|$Rbin|g;
	s|\@MEMCACHED\@|$memcached|g;
	print OUT;
}
close(IN);
close(OUT);
