use Tk::MMutil;
use Tk::Config;

Tk::MMutil::TkExtMakefile(
    'NAME'     => 'Tk::JPEG',
    'EXE_FILES'  => ['tkjpeg'],
    'OBJECT'     => '$(O_FILES)',
    'VERSION_FROM' => 'JPEG.pm',
    'XS_VERSION'   => $Tk::Config::VERSION,
    'MYEXTLIB' => 'jpeg/libjpeg.a',
    'dist'     => { COMPRESS => 'gzip -f9', SUFFIX => '.gz' },
    'clean'    => { FILES => 'jpeg/Makefile jpeg/config.status jpeg/jconfig.h' }  
);


sub MY::postamble {
'
$(MYEXTLIB)  : FORCE
	cd jpeg; $(MAKE) libjpeg.a
';
}


