package Tk::JPEG;
require DynaLoader;
require Tk;
require Tk::Image;
require Tk::Photo;


use vars qw($VERSION);
$VERSION = '1.002'; # $Id: //depot/tkJPEG/JPEG.pm#2$

@ISA = qw(DynaLoader);

bootstrap Tk::JPEG $Tk::VERSION;

1;

