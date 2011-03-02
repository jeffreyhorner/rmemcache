#!/usr/bin/perl

use strict;
use Cache::Memcached;
use Benchmark qw( timethis ) ;
use vars qw($memc);

$memc = new Cache::Memcached;
$memc->set_servers(['localhost:11211']);

die "memcached not running \n"
	unless $memc->set("x","012345678901234567890123456789");

timethis(1000,'$memc->replace("x","012345678901234567890123456789")');
