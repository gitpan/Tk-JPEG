package Tk::JPEG;
require DynaLoader;
require Tk;
require Tk::Image;
require Tk::Photo;

use vars qw($VERSION);
$VERSION = '1.005'; # $Id: //depot/tkJPEG/JPEG.pm#5$

@ISA = qw(DynaLoader);

bootstrap Tk::JPEG $Tk::VERSION;

1;

__END__

=head1 NAME

Tk::JPEG - JPEG loader for Tk::Photo 

=head1 SYNOPSIS

  use Tk;
  use Tk::JPEG;

  my $image = $widget->Photo('-format' => 'jpeg', -file => 'something.jpg');
  

=head1 DESCRIPTION

This is an extension for Tk400.* or Tk402.* which supplies
JPEG format loader for Photo image type.

JPEG access is via release 5 of the The Independent JPEG Group's (IJG)
free JPEG software.

=head1 AUTHOR

Nick Ing-Simmons E<lt>nick@ni-s.u-net.comE<gt>

=cut


