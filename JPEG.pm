package Tk::JPEG;
require DynaLoader;
require Tk;
require Tk::Image;
require Tk::Photo;

@ISA = qw(DynaLoader);

bootstrap Tk::JPEG $Tk::VERSION;

1;

