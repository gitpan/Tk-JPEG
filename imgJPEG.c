/*
 * imgJPEG.c --
 *
 *	A photo image file handler for JPEG files.
 *
 * This Tk image format handler reads and writes JPEG files in the standard
 * JFIF file format.  ("JPEG" should be the format name.)  It can also read
 * and write strings containing base64-encoded JPEG data.
 *
 * Several options can be provided in the format string, for example:
 *
 *	imageObject read input.jpg -shrink -format "jpeg -grayscale"
 *	imageObject write output.jpg -format "jpeg -quality 50 -progressive"
 *
 * The supported options for reading are:
 *	-fast:        Fast, low-quality processing
 *	-grayscale:   Force incoming image to grayscale
 * The supported options for writing are:
 *	-quality N:   Compression quality (0..100; 5-95 is useful range)
 *	              Default value: 75
 *	-smooth N:    Perform smoothing (10-30 is enough for most GIF's)
 *		      Default value: 0
 *	-grayscale:   Create monochrome JPEG file
 *	-optimize:    Optimize Huffman table
 *	-progressive: Create progressive JPEG file
 *
 *
 * Copyright (c) 1996-1997 Thomas G. Lane.
 * This file is based on tkImgPPM.c from the Tk 4.2 distribution.
 * That file is
 *	Copyright (c) 1994 The Australian National University.
 *	Copyright (c) 1994-1996 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * You will need a copy of the IJG JPEG library, version 5 or later,
 * to use this file.  If you didn't receive it with this package, see
 *	ftp://ftp.uu.net/graphics/jpeg/
 *
 * Author: Tom Lane (tgl@sss.pgh.pa.us)
 *
 * Modified for dynamical loading and for reading from channels by:
 *	Jan Nijtmans (nijtmans@worldaccess.nl)
 *
 * SCCS: @(#) imgJPEG.c
 */

/* system includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

/* Tk */
#include "pTk/imgInt.h"
#include <pTk/tkImgPhoto.h>
#include <pTk/tkImgPhoto.m>
#include "pTk/tkVMacro.h"

/* undef Tcl macros that conflict with libjpeg stuff (sigh) */
#undef EXTERN

/* libjpeg */
#ifdef MAC_TCL
#  include "libjpeg:jpeglib.h"
#  include "libjpeg:jerror.h"
#else
#  include <sys/types.h>
#ifdef HAVE_JPEGLIB_H
#  include <jpeglib.h>
#  include <jerror.h>
#else
#  include "jpeg/jpeglib.h"
#  include "jpeg/jerror.h"
#endif
#endif

#ifdef __WIN32__
#define JPEG_LIB_NAME "jpeg_dll"
#endif

#ifndef JPEG_LIB_NAME
#define JPEG_LIB_NAME "libjpeg.so"
#endif

/*
 * The format record for the JPEG file format:
 */

static int	ChnMatchJPEG _ANSI_ARGS_((Tcl_Interp *interp, Tcl_Channel chan, Arg fileName,
		    Arg formatString, int *widthPtr, int *heightPtr));
static int	FileMatchJPEG _ANSI_ARGS_((Tcl_Interp *interp, FILE *f, Arg fileName,
		    Arg formatString, int *widthPtr, int *heightPtr));
static int	ObjMatchJPEG _ANSI_ARGS_((Tcl_Interp *interp, struct Tcl_Obj *dataObj,
		    Arg formatString, int *widthPtr, int *heightPtr));
static int	ChnReadJPEG _ANSI_ARGS_((Tcl_Interp *interp,
		    Tcl_Channel chan, Arg fileName, Arg formatString,
		    Tk_PhotoHandle imageHandle, int destX, int destY,
		    int width, int height, int srcX, int srcY));
static int	FileReadJPEG _ANSI_ARGS_((Tcl_Interp *interp,
		    FILE *f, Arg fileName, Arg formatString,
		    Tk_PhotoHandle imageHandle, int destX, int destY,
		    int width, int height, int srcX, int srcY));
static int	ObjReadJPEG _ANSI_ARGS_((Tcl_Interp *interp,
		    struct Tcl_Obj *dataObj, Arg formatString,
		    Tk_PhotoHandle imageHandle, int destX, int destY,
		    int width, int height, int srcX, int srcY));
static int	FileWriteJPEG _ANSI_ARGS_((Tcl_Interp *interp,
		    char *fileName, Arg formatString,
		    Tk_PhotoImageBlock *blockPtr));
static int	StringWriteJPEG _ANSI_ARGS_((Tcl_Interp *interp,
		    Tcl_DString *dataPtr, Arg formatString,
		    Tk_PhotoImageBlock *blockPtr));

Tk_PhotoImageFormat imgFmtJPEG = {
    "JPEG",					/* name */
    ChnMatchJPEG,	/* fileMatchProc */
    ObjMatchJPEG,	/* stringMatchProc */
    ChnReadJPEG,	/* fileReadProc */
    ObjReadJPEG,	/* stringReadProc */
    FileWriteJPEG,	/* fileWriteProc */
    StringWriteJPEG	/* stringWriteProc */
};

/*
 * We use Tk_ParseArgv to parse any options supplied in the format string.
 */

static int fast;		/* static variables hold parse results */
static int grayscale;		/* ... icky, and not reentrant ... */
static int quality;
static int smooth;
static int optimize;
static int progressive;

static Tk_ArgvInfo readOptTable[] = {
    {"-fast", TK_ARGV_CONSTANT, (char *) 1, (char *) &fast,
	"Fast, low-quality processing"},
    {"-grayscale", TK_ARGV_CONSTANT, (char *) 1, (char *) &grayscale,
	"Force incoming image to grayscale"},
    {NULL, TK_ARGV_END,          NULL,          NULL,
	         NULL}
};

static Tk_ArgvInfo writeOptTable[] = {
    {"-quality", TK_ARGV_INT,          NULL, (char *) &quality,
	"Compression quality (0..100; 5-95 is useful range)"},
    {"-smooth", TK_ARGV_INT,          NULL, (char *) &smooth,
	"Smoothing factor (default = 0, 10-30 is enough for typical GIFs.)"},
    {"-grayscale", TK_ARGV_CONSTANT, (char *) 1, (char *) &grayscale,
	"Create monochrome JPEG file"},
    {"-optimize", TK_ARGV_CONSTANT, (char *) 1, (char *) &optimize,
	"Optimize Huffman table"},
    {"-progressive", TK_ARGV_CONSTANT, (char *) 1, (char *) &progressive,
	"Create progressive JPEG file"},
    {NULL, TK_ARGV_END,          NULL,          NULL,
	         NULL}
};

/*
 * Declarations for libjpeg source and destination managers to handle
 * reading and writing base64-encoded strings.
 */

#define STRING_BUF_SIZE  4096	/* choose any convenient size */

typedef struct str_source_mgr {	/* Source manager for reading from string */
  struct jpeg_source_mgr pub;	/* public fields */

  MFile handle;			/* base64 stream */
  JOCTET buffer[STRING_BUF_SIZE]; /* buffer for a chunk of decoded data */
} *str_src_ptr;

typedef struct str_destination_mgr { /* Manager for string output */
  struct jpeg_destination_mgr pub; /* public fields */

  MFile handle;			/* base64 stream */
  JOCTET buffer[STRING_BUF_SIZE]; /* buffer for a chunk of uncoded data */
} *str_dest_ptr;

/*
 * Declarations for libjpeg source and destination managers to handle
 * reading and writing channels.
 */

typedef struct chan_source_mgr {/* Source manager for reading from channels */
  struct jpeg_source_mgr pub;	/* public fields */

  Tcl_Channel chan;		/* channel */
  JOCTET buffer[STRING_BUF_SIZE]; /* buffer for a chunk of decoded data */
} *chan_src_ptr;

typedef struct chan_destination_mgr { /* Manager for channel output */
  struct jpeg_destination_mgr pub; /* public fields */

  Tcl_Channel chan;		/* channel */
  JOCTET buffer[STRING_BUF_SIZE]; /* buffer for a chunk of uncoded data */
} *chan_dest_ptr;

/*
 * Other declarations
 */

struct my_error_mgr {		/* Extended libjpeg error manager */
  struct jpeg_error_mgr pub;	/* public fields */
  jmp_buf setjmp_buffer;	/* for return to caller from error exit */
};

/*
 * Prototypes for local procedures defined in this file:
 */

static int	CommonMatchJPEG _ANSI_ARGS_((MFile *handle,
		    int *widthPtr, int *heightPtr));
static int	CommonReadJPEG _ANSI_ARGS_((Tcl_Interp *interp,
		    j_decompress_ptr cinfo, Arg formatString,
		    Tk_PhotoHandle imageHandle, int destX, int destY,
		    int width, int height, int srcX, int srcY));
static int	CommonWriteJPEG _ANSI_ARGS_((Tcl_Interp *interp,
		    j_compress_ptr cinfo, Arg formatString,
		    Tk_PhotoImageBlock *blockPtr));
static void	jpeg_obj_src _ANSI_ARGS_((j_decompress_ptr, struct Tcl_Obj *));
static boolean	str_fill_input_buffer _ANSI_ARGS_((j_decompress_ptr));
static void	str_skip_input_data _ANSI_ARGS_((j_decompress_ptr, long));
static void	dummy_source _ANSI_ARGS_((j_decompress_ptr));
static void	jpeg_channel_src _ANSI_ARGS_((j_decompress_ptr, Tcl_Channel));
static void	chan_init_source _ANSI_ARGS_((j_decompress_ptr));
static boolean	chan_fill_input_buffer _ANSI_ARGS_((j_decompress_ptr));
static void	chan_skip_input_data _ANSI_ARGS_((j_decompress_ptr, long));
static void	chan_term_source _ANSI_ARGS_((j_decompress_ptr));
static void	jpeg_string_dest _ANSI_ARGS_((j_compress_ptr, Tcl_DString*));
static void	str_init_destination _ANSI_ARGS_((j_compress_ptr));
static boolean	str_empty_output_buffer _ANSI_ARGS_((j_compress_ptr));
static void	str_term_destination _ANSI_ARGS_((j_compress_ptr));
static void	my_error_exit _ANSI_ARGS_((j_common_ptr cinfo));
static void	my_output_message _ANSI_ARGS_((j_common_ptr cinfo));
static void	append_jpeg_message _ANSI_ARGS_((Tcl_Interp *interp,
		    j_common_ptr cinfo));
static int	CreateCompress _ANSI_ARGS_((j_compress_ptr, int, size_t));
static int	CreateDecompress _ANSI_ARGS_((j_decompress_ptr, int, size_t));


int Imgjpeg_resync_to_restart _ANSI_ARGS_((j_decompress_ptr, int));
JDIMENSION Imgjpeg_read_scanlines _ANSI_ARGS_((j_decompress_ptr,
			JSAMPARRAY, JDIMENSION));
int Imgjpeg_set_colorspace _ANSI_ARGS_((j_compress_ptr, J_COLOR_SPACE));
int Imgjpeg_set_defaults _ANSI_ARGS_((j_compress_ptr));
int Imgjpeg_start_decompress _ANSI_ARGS_((j_decompress_ptr));
void Imgjpeg_destroy _ANSI_ARGS_((j_common_ptr));
struct jpeg_error_mgr * Imgjpeg_std_error _ANSI_ARGS_((struct jpeg_error_mgr *));
int Imgjpeg_CreateDecompress _ANSI_ARGS_((j_decompress_ptr, int, size_t));
JDIMENSION Imgjpeg_write_raw_data _ANSI_ARGS_((j_compress_ptr,
	    JSAMPIMAGE, JDIMENSION));
void Imgjpeg_suppress_tables _ANSI_ARGS_((j_compress_ptr, boolean));
void Imgjpeg_abort _ANSI_ARGS_((j_common_ptr));
int Imgjpeg_read_header _ANSI_ARGS_((j_decompress_ptr, int));
int Imgjpeg_start_compress _ANSI_ARGS_((j_compress_ptr, int));
void Imgjpeg_write_tables _ANSI_ARGS_((j_compress_ptr));
int Imgjpeg_finish_decompress _ANSI_ARGS_((j_decompress_ptr));
int Imgjpeg_CreateCompress _ANSI_ARGS_((j_compress_ptr, int, size_t));
int Imgjpeg_finish_compress _ANSI_ARGS_((j_compress_ptr));
JDIMENSION Imgjpeg_read_raw_data _ANSI_ARGS_((j_decompress_ptr,
	    JSAMPIMAGE, JDIMENSION));
int Imgjpeg_set_quality _ANSI_ARGS_((j_compress_ptr, int, int));
JDIMENSION Imgjpeg_write_scanlines _ANSI_ARGS_((j_compress_ptr,
			JSAMPARRAY, JDIMENSION));

static int
CreateCompress(cinfo, version, size)
    j_compress_ptr cinfo;
    int version;
    size_t size;
{
    jpeg_create_compress(cinfo);
    return 1;
}

static int
CreateDecompress(cinfo, version, size)
    j_decompress_ptr cinfo;
    int version;
    size_t size;
{
    jpeg_create_decompress(cinfo);
    return  1;
}

int
Imgjpeg_CreateCompress(cinfo, version, size)
    j_compress_ptr cinfo;
    int version;
    size_t size;
{
    jpeg_CreateCompress(cinfo, version, size);
    return 1;
}

int
Imgjpeg_CreateDecompress(cinfo, version, size)
    j_decompress_ptr cinfo;
    int version;
    size_t size;
{
    jpeg_CreateDecompress(cinfo, version, size);
    return 1;
}

int
Imgjpeg_resync_to_restart(a,b)
    j_decompress_ptr a;
    int b;
{
    return jpeg_resync_to_restart(a,b);
}

JDIMENSION
Imgjpeg_read_scanlines(a,b,c)
    j_decompress_ptr a;
    JSAMPARRAY b;
    JDIMENSION c;
{
    return jpeg_read_scanlines(a,b,c);
}

int
Imgjpeg_set_colorspace(a,b)
    j_compress_ptr a;
    J_COLOR_SPACE b;
{
    jpeg_set_colorspace(a,b);
    return 1;
}

int
Imgjpeg_set_defaults(a)
    j_compress_ptr a;
{
    jpeg_set_defaults(a);
    return 1;
}

int
Imgjpeg_start_decompress(a)
    j_decompress_ptr a;
{
    return jpeg_start_decompress(a);
}

void
Imgjpeg_destroy(a)
    j_common_ptr a;
{
    jpeg_destroy(a);
}

struct jpeg_error_mgr *
Imgjpeg_std_error(a)
    struct jpeg_error_mgr *a;
{
    return jpeg_std_error(a);
}

JDIMENSION Imgjpeg_write_raw_data(a,b,c)
    j_compress_ptr a;
    JSAMPIMAGE b;
    JDIMENSION c;
{
    return jpeg_write_raw_data(a,b,c);
}

void
Imgjpeg_suppress_tables(a,b)
    j_compress_ptr a;
    boolean b;
{
    jpeg_suppress_tables(a,b);
}

void
Imgjpeg_abort(a)
    j_common_ptr a;
{
    jpeg_abort(a);
}

int
Imgjpeg_read_header(a,b)
    j_decompress_ptr a;
    int b;
{
    return jpeg_read_header(a,b);
}

int
Imgjpeg_start_compress(a,b)
    j_compress_ptr a;
    int b;
{
    jpeg_start_compress(a,b);
    return 1;
}

void
Imgjpeg_write_tables(a)
    j_compress_ptr a;
{
    jpeg_write_tables(a);
}

int
Imgjpeg_finish_decompress(a)
    j_decompress_ptr a;
{
    return jpeg_finish_decompress(a);
}

int
Imgjpeg_finish_compress(a)
    j_compress_ptr a;
{
    jpeg_finish_compress(a);
    return 1;
}

JDIMENSION
Imgjpeg_read_raw_data(a,b,c)
    j_decompress_ptr a;
    JSAMPIMAGE b;
    JDIMENSION c;
{
    return jpeg_read_raw_data(a,b,c);
}

int
Imgjpeg_set_quality(a,b,c)
    j_compress_ptr a;
    int b;
    int c;
{
    jpeg_set_quality(a,b,c);
    return 1;
}

JDIMENSION
Imgjpeg_write_scanlines(a,b,c)
    j_compress_ptr a;
    JSAMPARRAY b;
    JDIMENSION c;
{
    return jpeg_write_scanlines(a,b,c);
}

/*
 *----------------------------------------------------------------------
 *
 * FileMatchJPEG --
 *
 *	This procedure is invoked by the photo image type to see if
 *	a file contains image data in JPEG format.
 *
 * Results:
 *	The return value is >0 if the first characters in file "f" look
 *	like JPEG data, and 0 otherwise.  For a valid file, the image
 *	dimensions are determined.
 *
 * Side effects:
 *	The access position in f may change.
 *
 *----------------------------------------------------------------------
 */

static int
FileMatchJPEG(interp, f, fileName, formatString, widthPtr, heightPtr)
    Tcl_Interp *interp;
    FILE *f;			/* The image file, open for reading. */
    Arg fileName;		/* The name of the image file. */
    Arg formatString;		/* User-specified format string, or NULL. */
    int *widthPtr, *heightPtr;	/* The dimensions of the image are
				 * returned here if the file is a valid
				 * JPEG file. */
{
    MFile handle;
    handle.data = (char *) f;
    handle.state = IMG_FILE;
    return CommonMatchJPEG(&handle, widthPtr, heightPtr);    
}

/*
 *----------------------------------------------------------------------
 *
 * ChnMatchJPEG --
 *
 *	This procedure is invoked by the photo image type to see if
 *	a channel contains image data in JPEG format.
 *
 * Results:
 *	The return value is >0 if the first characters in channel "chan"
 *	look like JPEG data, and 0 otherwise.  For a valid file, the
 *	image dimensions are determined.
 *
 * Side effects:
 *	The access position in f may change.
 *
 *----------------------------------------------------------------------
 */

static int
ChnMatchJPEG(interp, chan, fileName, formatString, widthPtr, heightPtr)
    Tcl_Interp *interp;
    Tcl_Channel chan;		/* The image channel, open for reading. */
    Arg fileName;		/* The name of the image file. */
    Arg formatString;		/* User-specified format string, or NULL. */
    int *widthPtr, *heightPtr;	/* The dimensions of the image are
				 * returned here if the file is a valid
				 * JPEG file. */
{
    MFile handle;
    handle.data = (char *) chan;
    handle.state = IMG_CHAN;
    return CommonMatchJPEG(&handle, widthPtr, heightPtr);
}
   

/*
 *----------------------------------------------------------------------
 *
 * ObjMatchJPEG --
 *
 *	This procedure is invoked by the photo image type to see if
 *	a string contains image data in JPEG format.
 *
 * Results:
 *	The return value is >0 if the first characters in the string look
 *	like JPEG data, and 0 otherwise.  For a valid image, the image
 *	dimensions are determined.
 *
 * Side effects:
 *  the size of the image is placed in widthPtr and heightPtr.
 *
 *----------------------------------------------------------------------
 */

static int
ObjMatchJPEG(interp, dataObj, formatString, widthPtr, heightPtr)
    Tcl_Interp *interp;
    struct Tcl_Obj *dataObj;	/* the object containing the image data */
    Arg formatString;		/* User-specified format string, or NULL. */
    int *widthPtr, *heightPtr;	/* The dimensions of the image are
				 * returned here if the string is a valid
				 * JPEG image. */
{
    MFile handle;
    ImgReadInit(dataObj, '\377', &handle);
    return CommonMatchJPEG(&handle, widthPtr, heightPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * CommonMatchJPEG --
 *
 *	This procedure is invoked by the photo image type to see if
 *	a string contains image data in JPEG format.
 *
 * Results:
 *	The return value is >0 if the first characters in the string look
 *	like JPEG data, and 0 otherwise.  For a valid image, the image
 *	dimensions are determined.
 *
 * Side effects:
 *  the size of the image is placed in widthPtr and heightPtr.
 *
 *----------------------------------------------------------------------
 */

static int
CommonMatchJPEG(handle, widthPtr, heightPtr)
    MFile *handle;		/* the "file" handle */
    int *widthPtr, *heightPtr;	/* The dimensions of the image are
				 * returned here if the string is a valid
				 * JPEG image. */
{
    char buf[256];
    int i;

    i = ImgRead(handle, buf, 3);
    if ((i != 3)||strncmp(buf,"\377\330\377", 3)) {
	return 0;
    }

    buf[0] = buf[2];
    /* at top of loop: have just read first FF of a marker into buf[0] */
    for (;;) {
	/* get marker type byte, skipping any padding FFs */
	while (buf[0] == (char) 0xff) {
	    if (ImgRead(handle, buf,1) != 1) {
		return 0;
	    }
	}
	/* look for SOF0, SOF1, or SOF2, which are the only JPEG variants
	 * currently accepted by libjpeg.
	 */
	if (buf[0] == (char) 0xc0 || buf[0] == (char) 0xc1
		|| buf[0] == (char) 0xc2)
	    break;
	/* nope, skip the marker parameters */
	if (ImgRead(handle, buf, 2) != 2) {
	    return 0;
	}
	i = ((buf[0] & 0x0ff)<<8) + (buf[1] & 0x0ff) - 1;
	while (i>256) {
	    ImgRead(handle, buf, 256);
	    i -= 256;
	}
	if ((i<1) || (ImgRead(handle, buf, i)) != i) {
	    return 0;
	}
	buf[0] = buf[i-1];
	/* skip any inter-marker junk (there shouldn't be any, really) */
	while (buf[0] != (char) 0xff) {
	    if (ImgRead(handle, buf,1) != 1) {
		return 0;
	    }
	}
    }
    /* Found the SOFn marker, get image dimensions */
    if (ImgRead(handle, buf, 7) != 7) {
	return 0;
    }
    *heightPtr = ((buf[3] & 0x0ff)<<8) + (buf[4] & 0x0ff);
    *widthPtr = ((buf[5] & 0x0ff)<<8) + (buf[6] & 0x0ff);

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * ChnReadJPEG --
 *
 *	This procedure is called by the photo image type to read
 *	JPEG format data from a channel, and give it to
 *	the photo image.
 *
 * Results:
 *	A standard TCL completion code.  If TCL_ERROR is returned
 *	then an error message is left in Tcl_GetResult(interp).
 *
 * Side effects:
 *	New data is added to the image given by imageHandle.
 *
 *----------------------------------------------------------------------
 */

static int
ChnReadJPEG(interp, chan, fileName, formatString, imageHandle, destX, destY,
	width, height, srcX, srcY)
    Tcl_Interp *interp;		/* Interpreter to use for reporting errors. */
    Tcl_Channel chan;		/* The image channel, open for reading. */
    Arg fileName;		/* The name of the image file. */
    Arg formatString;		/* User-specified format string, or NULL. */
    Tk_PhotoHandle imageHandle;	/* The photo image to write into. */
    int destX, destY;		/* Coordinates of top-left pixel in
				 * photo image to be written to. */
    int width, height;		/* Dimensions of block of photo image to
				 * be written to. */
    int srcX, srcY;		/* Coordinates of top-left pixel to be used
				 * in image being read. */
{
    struct jpeg_decompress_struct cinfo; /* libjpeg's parameter structure */
    struct my_error_mgr jerror;	/* for controlling libjpeg error handling */
    int result;


    /* Initialize JPEG error handler */
    /* We set up the normal JPEG error routines, then override error_exit. */
    cinfo.err = jpeg_std_error(&jerror.pub);
    jerror.pub.error_exit = my_error_exit;
    jerror.pub.output_message = my_output_message;

    /* Establish the setjmp return context for my_error_exit to use. */
    if (setjmp(jerror.setjmp_buffer)) {
      /* If we get here, the JPEG code has signaled an error. */
      Tcl_AppendResult(interp, "couldn't read JPEG string: ",          NULL);
      append_jpeg_message(interp, (j_common_ptr) &cinfo);
      jpeg_destroy_decompress(&cinfo);
      return TCL_ERROR;
    }

    /* Now we can initialize libjpeg. */
    jpeg_CreateDecompress(&cinfo, JPEG_LIB_VERSION,
			(size_t) sizeof(struct jpeg_decompress_struct));
    jpeg_channel_src(&cinfo, chan);

    /* Share code with FileReadJPEG. */
    result = CommonReadJPEG(interp, &cinfo, formatString, imageHandle,
			    destX, destY, width, height, srcX, srcY);

    /* Reclaim libjpeg's internal resources. */
    jpeg_destroy_decompress(&cinfo);

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * FileReadJPEG --
 *
 *	This procedure is called by the photo image type to read
 *	JPEG format data from a file and write it into a given
 *	photo image.
 *
 * Results:
 *	A standard TCL completion code.  If TCL_ERROR is returned
 *	then an error message is left in Tcl_GetResult(interp).
 *
 * Side effects:
 *	The access position in file f is changed, and new data is
 *	added to the image given by imageHandle.
 *
 *----------------------------------------------------------------------
 */

static int
FileReadJPEG(interp, f, fileName, formatString, imageHandle, destX, destY,
	width, height, srcX, srcY)
    Tcl_Interp *interp;		/* Interpreter to use for reporting errors. */
    FILE *f;			/* The image file, open for reading. */
    Arg fileName;		/* The name of the image file. */
    Arg formatString;		/* User-specified format string, or NULL. */
    Tk_PhotoHandle imageHandle;	/* The photo image to write into. */
    int destX, destY;		/* Coordinates of top-left pixel in
				 * photo image to be written to. */
    int width, height;		/* Dimensions of block of photo image to
				 * be written to. */
    int srcX, srcY;		/* Coordinates of top-left pixel to be used
				 * in image being read. */
{
    struct jpeg_decompress_struct cinfo; /* libjpeg's parameter structure */
    struct my_error_mgr jerror;	/* for controlling libjpeg error handling */
    int result;


    /* Initialize JPEG error handler */
    /* We set up the normal JPEG error routines, then override error_exit. */
    cinfo.err = jpeg_std_error(&jerror.pub);
    jerror.pub.error_exit = my_error_exit;
    jerror.pub.output_message = my_output_message;

    /* Establish the setjmp return context for my_error_exit to use. */
    if (setjmp(jerror.setjmp_buffer)) {
      /* If we get here, the JPEG code has signaled an error. */
      Tcl_AppendResult(interp, "couldn't read JPEG file \"", fileName,
		       "\": ",          NULL);
      append_jpeg_message(interp, (j_common_ptr) &cinfo);
      jpeg_destroy_decompress(&cinfo);
      return TCL_ERROR;
    }

    /* Now we can initialize libjpeg. */
    jpeg_CreateDecompress(&cinfo, JPEG_LIB_VERSION,
			(size_t) sizeof(struct jpeg_decompress_struct));
    jpeg_stdio_src(&cinfo, f);

    /* Share code with StringReadJPEG. */
    result = CommonReadJPEG(interp, &cinfo, formatString, imageHandle,
			    destX, destY, width, height, srcX, srcY);

    /* Reclaim libjpeg's internal resources. */
    jpeg_destroy_decompress(&cinfo);

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ObjReadJPEG --
 *
 *	This procedure is called by the photo image type to read
 *	JPEG format data from a base64 encoded string, and give it to
 *	the photo image.
 *
 * Results:
 *	A standard TCL completion code.  If TCL_ERROR is returned
 *	then an error message is left in Tcl_GetResult(interp).
 *
 * Side effects:
 *	New data is added to the image given by imageHandle.
 *
 *----------------------------------------------------------------------
 */

static int
ObjReadJPEG(interp, dataObj, formatString, imageHandle, destX, destY,
	width, height, srcX, srcY)
    Tcl_Interp *interp;		/* Interpreter to use for reporting errors. */
    struct Tcl_Obj *dataObj;	/* Object containing the image data. */
    Arg formatString;		/* User-specified format string, or NULL. */
    Tk_PhotoHandle imageHandle;	/* The photo image to write into. */
    int destX, destY;		/* Coordinates of top-left pixel in
				 * photo image to be written to. */
    int width, height;		/* Dimensions of block of photo image to
				 * be written to. */
    int srcX, srcY;		/* Coordinates of top-left pixel to be used
				 * in image being read. */
{
    struct jpeg_decompress_struct cinfo; /* libjpeg's parameter structure */
    struct my_error_mgr jerror;	/* for controlling libjpeg error handling */
    int result;


    /* Initialize JPEG error handler */
    /* We set up the normal JPEG error routines, then override error_exit. */
    cinfo.err = jpeg_std_error(&jerror.pub);
    jerror.pub.error_exit = my_error_exit;
    jerror.pub.output_message = my_output_message;

    /* Establish the setjmp return context for my_error_exit to use. */
    if (setjmp(jerror.setjmp_buffer)) {
      /* If we get here, the JPEG code has signaled an error. */
      Tcl_AppendResult(interp, "couldn't read JPEG string: ",          NULL);
      append_jpeg_message(interp, (j_common_ptr) &cinfo);
      jpeg_destroy_decompress(&cinfo);
      return TCL_ERROR;
    }

    /* Now we can initialize libjpeg. */
    jpeg_CreateDecompress(&cinfo, JPEG_LIB_VERSION,
			(size_t) sizeof(struct jpeg_decompress_struct));
    jpeg_obj_src(&cinfo, dataObj);

    /* Share code with FileReadJPEG. */
    result = CommonReadJPEG(interp, &cinfo, formatString, imageHandle,
			    destX, destY, width, height, srcX, srcY);

    /* Reclaim libjpeg's internal resources. */
    jpeg_destroy_decompress(&cinfo);

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * CommonReadJPEG --
 *
 *	The common guts of ChnReadJPEG, FileReadJPEG and ObjReadJPEG.
 *	The decompress struct has already been set up and the
 *	appropriate data source manager initialized.
 *	The caller should do jpeg_destroy_decompress upon return.
 *
 *----------------------------------------------------------------------
 */

typedef struct myblock {
    Tk_PhotoImageBlock ck;
    int dummy; /* extra space for offset[3], if not included already
		  in Tk_PhotoImageBlock */
} myblock;

static int
CommonReadJPEG(interp, cinfo, formatString, imageHandle, destX, destY,
	width, height, srcX, srcY)
    Tcl_Interp *interp;		/* Interpreter to use for reporting errors. */
    j_decompress_ptr cinfo;	/* Already-constructed decompress struct. */
    Arg formatString;		/* User-specified format string, or NULL. */
    Tk_PhotoHandle imageHandle;	/* The photo image to write into. */
    int destX, destY;		/* Coordinates of top-left pixel in
				 * photo image to be written to. */
    int width, height;		/* Dimensions of block of photo image to
				 * be written to. */
    int srcX, srcY;		/* Coordinates of top-left pixel to be used
				 * in image being read. */
{
    int fileWidth, fileHeight, stopY, curY, outY, outWidth, outHeight;
    myblock bl;
#define block bl.ck
    JSAMPARRAY buffer;		/* Output row buffer */

    /* Ready to read header data. */
    jpeg_read_header(cinfo, TRUE);

    /* This code only supports 8-bit-precision JPEG files. */
    if ((cinfo->data_precision != 8) ||
	(sizeof(JSAMPLE) != sizeof(unsigned char))) {
      Tcl_AppendResult(interp, "Unsupported JPEG precision",          NULL);
      return TCL_ERROR;
    }

    /* Process format parameters to adjust decompression options. */
    if (formatString != NULL) {
      int argc = 0;
      Arg *args;    
      if (Tcl_ListObjGetElements(interp, formatString, &argc, &args) != TCL_OK)
	return TCL_ERROR;
      fast = 0;
      grayscale = 0;
#if 0
      if (Tk_ParseArgv(interp, (Tk_Window) NULL, &argc, args,
		       readOptTable, TK_ARGV_NO_LEFTOVERS|TK_ARGV_NO_DEFAULTS)
	  != TCL_OK) {
	return TCL_ERROR;
      }
      if (fast) {
	/* Select recommended processing options for quick-and-dirty output. */
	cinfo->two_pass_quantize = FALSE;
	cinfo->dither_mode = JDITHER_ORDERED;
	cinfo->dct_method = JDCT_FASTEST;
	cinfo->do_fancy_upsampling = FALSE;
      }
      if (grayscale) {
	/* Force monochrome output. */
	cinfo->out_color_space = JCS_GRAYSCALE;
      }
#endif
    }

    jpeg_start_decompress(cinfo);

    /* Check dimensions. */
    fileWidth = (int) cinfo->output_width;
    fileHeight = (int) cinfo->output_height;
    if ((srcX + width) > fileWidth) {
	outWidth = fileWidth - srcX;
    } else {
	outWidth = width;
    }
    if ((srcY + height) > fileHeight) {
	outHeight = fileHeight - srcY;
    } else {
	outHeight = height;
    }
    if ((outWidth <= 0) || (outHeight <= 0)
	|| (srcX >= fileWidth) || (srcY >= fileHeight)) {
	return TCL_OK;
    }

    /* Check colorspace. */
    switch (cinfo->out_color_space) {
    case JCS_GRAYSCALE:
      /* a single-sample grayscale pixel is expanded into equal R,G,B values */
      block.pixelSize = 1;
      block.offset[0] = 0;
      block.offset[1] = 0;
      block.offset[2] = 0;
      break;
    case JCS_RGB:
      /* note: this pixel layout assumes default configuration of libjpeg. */
      block.pixelSize = 3;
      block.offset[0] = 0;
      block.offset[1] = 1;
      block.offset[2] = 2;
      break;
    default:
      Tcl_AppendResult(interp, "Unsupported JPEG color space",          NULL);
      return TCL_ERROR;
    }
    block.width = outWidth;
    block.height = 1;
    block.pitch = block.pixelSize * fileWidth;
    block.offset[3] = 0;

    Tk_PhotoExpand(imageHandle, destX + outWidth, destY + outHeight);

    /* Make a temporary one-row-high sample array */
    buffer = (*cinfo->mem->alloc_sarray)
		((j_common_ptr) cinfo, JPOOL_IMAGE,
		 cinfo->output_width * cinfo->output_components, 1);
    block.pixelPtr = (unsigned char *) buffer[0] + srcX * block.pixelSize;

    /* Read as much of the data as we need to */
    stopY = srcY + outHeight;
    outY = destY;
    for (curY = 0; curY < stopY; curY++) {
      jpeg_read_scanlines(cinfo, buffer, 1);
      if (curY >= srcY) {
	Tk_PhotoPutBlock(imageHandle, &block, destX, outY, outWidth, 1);
	outY++;
      }
    }

    /* Do normal cleanup if we read the whole image; else early abort */
    if (cinfo->output_scanline == cinfo->output_height)
	jpeg_finish_decompress(cinfo);
    else
	jpeg_abort_decompress(cinfo);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * FileWriteJPEG --
 *
 *	This procedure is invoked to write image data to a file in JPEG
 *	format.
 *
 * Results:
 *	A standard TCL completion code.  If TCL_ERROR is returned
 *	then an error message is left in Tcl_GetResult(interp).
 *
 * Side effects:
 *	Data is written to the file given by "fileName".
 *
 *----------------------------------------------------------------------
 */

static int
FileWriteJPEG(interp, fileName, formatString, blockPtr)
    Tcl_Interp *interp;
    char *fileName;
    Arg formatString;
    Tk_PhotoImageBlock *blockPtr;
{
    struct jpeg_compress_struct cinfo; /* libjpeg's parameter structure */
    struct my_error_mgr jerror;	/* for controlling libjpeg error handling */
    Tcl_DString nameBuffer; 
    char *fullName;
    FILE *f;
    int result;

    if ((fullName=Tcl_TranslateFileName(interp,fileName,&nameBuffer))==NULL) {
	return TCL_ERROR;
    }
    if ((f = fopen(fullName, "wb")) == NULL) {
	Tcl_AppendResult(interp, fileName, ": ", Tcl_PosixError(interp),
			          NULL);
	Tcl_DStringFree(&nameBuffer);
	return TCL_ERROR;
    }
    Tcl_DStringFree(&nameBuffer);


    /* Initialize JPEG error handler */
    /* We set up the normal JPEG error routines, then override error_exit. */
    cinfo.err = jpeg_std_error(&jerror.pub);
    jerror.pub.error_exit = my_error_exit;
    jerror.pub.output_message = my_output_message;

    /* Establish the setjmp return context for my_error_exit to use. */
    if (setjmp(jerror.setjmp_buffer)) {
      /* If we get here, the JPEG code has signaled an error. */
      Tcl_AppendResult(interp, "couldn't write JPEG file \"", fileName,
		       "\": ",          NULL);
      append_jpeg_message(interp, (j_common_ptr) &cinfo);
      jpeg_destroy_compress(&cinfo);
      fclose(f);
      return TCL_ERROR;
    }

    /* Now we can initialize libjpeg. */
    jpeg_CreateCompress(&cinfo, JPEG_LIB_VERSION,
			(size_t) sizeof(struct jpeg_compress_struct));
    jpeg_stdio_dest(&cinfo, f);

    /* Share code with StringWriteJPEG. */
    result = CommonWriteJPEG(interp, &cinfo, formatString, blockPtr);

    jpeg_destroy_compress(&cinfo);

    fclose(f);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringWriteJPEG --
 *
 *	This procedure is called by the photo image type to write
 *	JPEG format data to a base-64 encoded string from the photo block.
 *
 * Results:
 *	A standard TCL completion code.  If TCL_ERROR is returned
 *	then an error message is left in Tcl_GetResult(interp).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
StringWriteJPEG(interp, dataPtr, formatString, blockPtr)
    Tcl_Interp *interp;
    Tcl_DString *dataPtr;
    Arg formatString;
    Tk_PhotoImageBlock *blockPtr;
{
    struct jpeg_compress_struct cinfo; /* libjpeg's parameter structure */
    struct my_error_mgr jerror;	/* for controlling libjpeg error handling */
    int result;

    /* Initialize JPEG error handler */
    /* We set up the normal JPEG error routines, then override error_exit. */
    cinfo.err = jpeg_std_error(&jerror.pub);
    jerror.pub.error_exit = my_error_exit;
    jerror.pub.output_message = my_output_message;

    /* Establish the setjmp return context for my_error_exit to use. */
    if (setjmp(jerror.setjmp_buffer)) {
      /* If we get here, the JPEG code has signaled an error. */
      Tcl_AppendResult(interp, "couldn't write JPEG string: ",          NULL);
      append_jpeg_message(interp, (j_common_ptr) &cinfo);
      jpeg_destroy_compress(&cinfo);
      return TCL_ERROR;
    }

    /* Now we can initialize libjpeg. */
    jpeg_CreateCompress(&cinfo, JPEG_LIB_VERSION,
	    (size_t) sizeof(struct jpeg_compress_struct));
    jpeg_string_dest(&cinfo, dataPtr);

    /* Share code with FileWriteJPEG. */
    result = CommonWriteJPEG(interp, &cinfo, formatString, blockPtr);

    jpeg_destroy_compress(&cinfo);

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * CommonWriteJPEG --
 *
 *	The common guts of FileWriteJPEG and StringWriteJPEG.
 *	The compress struct has already been set up and the
 *	appropriate data destination manager initialized.
 *	The caller should do jpeg_destroy_compress upon return,
 *	and also close the destination as necessary.
 *
 *----------------------------------------------------------------------
 */

static int
CommonWriteJPEG(interp, cinfo, formatString, blockPtr)
    Tcl_Interp *interp;
    j_compress_ptr cinfo;	
    Arg formatString;
    Tk_PhotoImageBlock *blockPtr;
{
    JSAMPROW row_pointer[1];	/* pointer to original data scanlines */
    JSAMPARRAY buffer;		/* Intermediate row buffer */
    JSAMPROW bufferPtr;
    int w, h;
    int greenOffset, blueOffset, alphaOffset;
    unsigned char *pixelPtr, *pixLinePtr;

    grayscale = 0;
    greenOffset = blockPtr->offset[1] - blockPtr->offset[0];
    blueOffset = blockPtr->offset[2] - blockPtr->offset[0];
    alphaOffset = blockPtr->offset[0];
    if (alphaOffset < blockPtr->offset[2]) {
        alphaOffset = blockPtr->offset[2];
    }
    if (++alphaOffset < blockPtr->pixelSize) {
	alphaOffset -= blockPtr->offset[0];
    } else {
	alphaOffset = 0;
    }

    /* Set up JPEG compression parameters. */
    cinfo->image_width = blockPtr->width;
    cinfo->image_height = blockPtr->height;
    cinfo->input_components = 3;
    cinfo->in_color_space = JCS_RGB;

    jpeg_set_defaults(cinfo);

    /* Parse options, if any, and alter default parameters */
    if (formatString != NULL) {
      int argc = 0;
      Arg *args;
      if (Tcl_ListObjGetElements(interp, formatString, &argc, &args) != TCL_OK)
	return TCL_ERROR;
      quality = 75;		/* default values */
      smooth = 0;
      optimize = 0;
      progressive = 0;
#if 0
      if (Tk_ParseArgv(interp, (Tk_Window) NULL, &argc, args,
		       writeOptTable, TK_ARGV_NO_LEFTOVERS|TK_ARGV_NO_DEFAULTS)
	  != TCL_OK) {
	return TCL_ERROR;
      }
      if (jpeg_set_quality != NULL) {
	jpeg_set_quality(cinfo, quality, FALSE);
      }
      cinfo->smoothing_factor = smooth;
      if (grayscale && (jpeg_set_colorspace != NULL)) {
	/* Force a monochrome JPEG file to be generated. */
	jpeg_set_colorspace(cinfo, JCS_GRAYSCALE);
      }
      if (optimize) {
	/* Enable entropy parm optimization. */
	cinfo->optimize_coding = TRUE;
      }
      if (progressive  && (jpeg_simple_progression != NULL)) {
	/* Select simple progressive mode. */
	jpeg_simple_progression(cinfo);
      }
#endif
    }

    pixLinePtr = blockPtr->pixelPtr + blockPtr->offset[0];
    greenOffset = blockPtr->offset[1] - blockPtr->offset[0];
    blueOffset = blockPtr->offset[2] - blockPtr->offset[0];
    if ((jpeg_set_colorspace != NULL) &&
	    (grayscale || (!greenOffset && !blueOffset))) {
	/* Generate monochrome JPEG file if source block is grayscale. */
	jpeg_set_colorspace(cinfo, JCS_GRAYSCALE);
    }

    jpeg_start_compress(cinfo, TRUE);
    
    /* note: we assume libjpeg is configured for standard RGB pixel order. */
    if ((greenOffset == 1) && (blueOffset == 2)
	&& (blockPtr->pixelSize == 3)) {
	/* No need to reformat pixels before passing data to libjpeg */
	for (h = blockPtr->height; h > 0; h--) {
	    row_pointer[0] = (JSAMPROW) pixLinePtr;
	    jpeg_write_scanlines(cinfo, row_pointer, 1);
	    pixLinePtr += blockPtr->pitch;
	}
    } else {
	/* Must convert data format.  Create a one-scanline work buffer. */
	buffer = (*cinfo->mem->alloc_sarray)
	  ((j_common_ptr) cinfo, JPOOL_IMAGE,
	   cinfo->image_width * cinfo->input_components, 1);
	for (h = blockPtr->height; h > 0; h--) {
	    pixelPtr = pixLinePtr;
	    bufferPtr = buffer[0];
	    for (w = blockPtr->width; w > 0; w--) {
		if (alphaOffset && !pixelPtr[alphaOffset]) {
		    /* if pixel is transparant, better use gray
		     * than the default black.
		     */
		    *bufferPtr++ = 0xd9;
		    *bufferPtr++ = 0xd9;
		    *bufferPtr++ = 0xd9;
		} else {
		    *bufferPtr++ = pixelPtr[0];
		    *bufferPtr++ = pixelPtr[greenOffset];
		    *bufferPtr++ = pixelPtr[blueOffset];
		}
		pixelPtr += blockPtr->pixelSize;
	    }
	    jpeg_write_scanlines(cinfo, buffer, 1);
	    pixLinePtr += blockPtr->pitch;
	}
    }

    jpeg_finish_compress(cinfo);

    return TCL_OK;
}

/*
 * libjpeg source manager for reading from base64-encoded strings.
 */
static void
jpeg_obj_src (cinfo, dataObj)
    j_decompress_ptr cinfo;
    struct Tcl_Obj *dataObj;
{
  str_src_ptr src;

  src = (str_src_ptr)
      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
				  sizeof(struct str_source_mgr));
  cinfo->src = (struct jpeg_source_mgr *) src;

  src->pub.init_source = dummy_source;
  src->pub.fill_input_buffer = str_fill_input_buffer;
  src->pub.skip_input_data = str_skip_input_data;
  src->pub.resync_to_restart = jpeg_resync_to_restart; /* use default method */
  src->pub.term_source = dummy_source;

  ImgReadInit(dataObj, '\377', &src->handle);

  src->pub.bytes_in_buffer = 0; /* forces fill_input_buffer on first read */
  src->pub.next_input_byte = NULL; /* until buffer loaded */
}

static boolean
str_fill_input_buffer(cinfo)
    j_decompress_ptr cinfo;
{
  str_src_ptr src = (str_src_ptr) cinfo->src;
  int nbytes;
  int c;

  nbytes = 0;
  while (nbytes < STRING_BUF_SIZE &&
	 (c = ImgGetc(&src->handle)) != IMG_DONE) {
    src->buffer[nbytes++] = (JOCTET) c;
  }

  if (nbytes <= 0) {
    /* Insert a fake EOI marker */
    src->buffer[0] = (JOCTET) 0xFF;
    src->buffer[1] = (JOCTET) JPEG_EOI;
    nbytes = 2;
  }

  src->pub.next_input_byte = src->buffer;
  src->pub.bytes_in_buffer = nbytes;

  return TRUE;
}

static void
str_skip_input_data(cinfo, num_bytes)
    j_decompress_ptr cinfo;
    long num_bytes;
{
  str_src_ptr src = (str_src_ptr) cinfo->src;

  if (num_bytes > 0) {
    while (num_bytes > (long) src->pub.bytes_in_buffer) {
      num_bytes -= (long) src->pub.bytes_in_buffer;
      str_fill_input_buffer(cinfo);
    }
    src->pub.next_input_byte += (size_t) num_bytes;
    src->pub.bytes_in_buffer -= (size_t) num_bytes;
  }
}

static void
dummy_source(cinfo)
    j_decompress_ptr cinfo;
{
  /* no work necessary here */
}

/*
 * libjpeg source manager for reading from channels.
 */
static void
jpeg_channel_src (cinfo, chan)
    j_decompress_ptr cinfo;
    Tcl_Channel chan;
{
  chan_src_ptr src;

  src = (chan_src_ptr)
      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
				  sizeof(struct chan_source_mgr));
  cinfo->src = (struct jpeg_source_mgr *) src;

  src->pub.init_source = chan_init_source;
  src->pub.fill_input_buffer = chan_fill_input_buffer;
  src->pub.skip_input_data = chan_skip_input_data;
  src->pub.resync_to_restart = jpeg_resync_to_restart; /* use default method */
  src->pub.term_source = chan_term_source;

  src->chan = chan;

  src->pub.bytes_in_buffer = 0; /* forces fill_input_buffer on first read */
  src->pub.next_input_byte = NULL; /* until buffer loaded */
}

static void
chan_init_source(cinfo)
    j_decompress_ptr cinfo;
{
  /* no work necessary here */
}

static boolean
chan_fill_input_buffer(cinfo)
    j_decompress_ptr cinfo;
{
  chan_src_ptr src = (chan_src_ptr) cinfo->src;
  int nbytes;

  nbytes = Tcl_Read(src->chan, src->buffer, STRING_BUF_SIZE);

  if (nbytes <= 0) {
    WARNMS(cinfo, JWRN_JPEG_EOF);
    /* Insert a fake EOI marker */
    src->buffer[0] = (JOCTET) 0xFF;
    src->buffer[1] = (JOCTET) JPEG_EOI;
    nbytes = 2;
  }

  src->pub.next_input_byte = src->buffer;
  src->pub.bytes_in_buffer = (size_t) nbytes;

  return TRUE;
}

static void
chan_skip_input_data(cinfo, num_bytes)
    j_decompress_ptr cinfo;
    long num_bytes;
{
  chan_src_ptr src = (chan_src_ptr) cinfo->src;

  if (num_bytes > 0) {
    while (num_bytes > (long) src->pub.bytes_in_buffer) {
      num_bytes -= (long) src->pub.bytes_in_buffer;
      chan_fill_input_buffer(cinfo);
    }
    src->pub.next_input_byte += num_bytes;
    src->pub.bytes_in_buffer -= num_bytes;
  }
}

static void
chan_term_source(cinfo)
    j_decompress_ptr cinfo;
{
  /* no work necessary here */
}

/*
 * libjpeg destination manager for writing to base64-encoded strings.
 */
static void
jpeg_string_dest (cinfo, dstring)
    j_compress_ptr cinfo;
    Tcl_DString* dstring;
{
  str_dest_ptr dest;

  if (cinfo->dest == NULL) {	/* first time for this JPEG object? */
    cinfo->dest = (struct jpeg_destination_mgr *)
      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
				  sizeof(struct str_destination_mgr));
  }

  dest = (str_dest_ptr) cinfo->dest;
  dest->pub.init_destination = str_init_destination;
  dest->pub.empty_output_buffer = str_empty_output_buffer;
  dest->pub.term_destination = str_term_destination;
  dest->handle.buffer = dstring;
}

static void
str_init_destination (cinfo)
    j_compress_ptr cinfo;
{
  str_dest_ptr dest = (str_dest_ptr) cinfo->dest;

  Tcl_DStringSetLength(dest->handle.buffer, STRING_BUF_SIZE);
  dest->handle.data = Tcl_DStringValue(dest->handle.buffer);
  dest->handle.state = 0;
  dest->handle.length = 0;
  dest->pub.next_output_byte = dest->buffer;
  dest->pub.free_in_buffer = STRING_BUF_SIZE;
}

static boolean
str_empty_output_buffer (cinfo)
    j_compress_ptr cinfo;
{
  str_dest_ptr dest = (str_dest_ptr) cinfo->dest;

  if (ImgWrite(&dest->handle, (char *) dest->buffer, STRING_BUF_SIZE)
  	!= STRING_BUF_SIZE)
    ERREXIT(cinfo, JERR_FILE_WRITE);

  dest->pub.next_output_byte = dest->buffer;
  dest->pub.free_in_buffer = STRING_BUF_SIZE;

  return TRUE;
}

static void
str_term_destination (cinfo)
    j_compress_ptr cinfo;
{
  str_dest_ptr dest = (str_dest_ptr) cinfo->dest;
  int datacount = STRING_BUF_SIZE - (int) dest->pub.free_in_buffer;

  /* Write any data remaining in the buffer */
  if (datacount > 0) {
    if (ImgWrite(&dest->handle, (char *) dest->buffer, datacount)
	!= datacount)
      ERREXIT(cinfo, JERR_FILE_WRITE);
  }

  /* Empty any partial-byte from the base64 encoder */
  ImgPutc(IMG_DONE, &dest->handle);
}


/*
 * Error handler to replace (or extend, really) libjpeg's default handler
 */

static void
my_error_exit (cinfo)
    j_common_ptr cinfo;
{
  struct my_error_mgr *myerr = (struct my_error_mgr *) cinfo->err;
  /* Exit back to outer level */
  longjmp(myerr->setjmp_buffer, 1);
}

static void
append_jpeg_message (interp, cinfo)
    Tcl_Interp *interp;
    j_common_ptr cinfo;
{
  /* Append libjpeg error message to Tcl_GetResult(interp) */
  char buffer[JMSG_LENGTH_MAX];
  (*cinfo->err->format_message) (cinfo, buffer);
  Tcl_AppendResult(interp, buffer,          NULL);
}

static void
my_output_message (cinfo)
    j_common_ptr cinfo;
{
  /* Override libjpeg's output_message to do nothing.
   * This ensures that warning messages will not appear on stderr,
   * even for a corrupted JPEG file.  Too bad there's no way
   * to report a "warning" message to the calling Tcl script.
   */
}
