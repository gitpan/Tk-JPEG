use Tk::MMutil;

Tk::MMutil::TkExtMakefile(
    'NAME'     => 'Tk::JPEG',
    'OBJECT'     => '$(O_FILES)',
    'VERSION_FROM' => 'JPEG.pm',
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


