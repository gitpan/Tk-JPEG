/*
 * tkImgFmtJPG.c --
 *
 *	A photo image file handler for JPEG files.
 *
 */

#ifndef __GNUC__
#define volatile
#endif

static char sccsid[] = "@(#) tkImgFmtJPG.c 1.9 95/08/30 15:34:52";

#include <setjmp.h>
#include <pTk/tkPort.h>
#include <pTk/tkInt.h>
#include <pTk/tkImgPhoto.h>
#include <pTk/tkVMacro.h>
#include "jpeg/jpeglib.h"

/*
 * The maximum amount of memory to allocate for data read from the
 * file.  If we need more than this, we do it in pieces.
 */

#define MAX_MEMORY	10000		/* don't allocate > 10KB */

/*
 * Define PGM and JPG, i.e. gray images and color images.
 */

#define PGM 1
#define JPG 2

/*
 * The format record for the JPG file format:
 */

static int		FileMatchJPG _ANSI_ARGS_((FILE *f, char *fileName,
			    char *formatString, int *widthPtr,
			    int *heightPtr));
static int		FileReadJPG  _ANSI_ARGS_((Tcl_Interp *interp,
			    FILE *f, char *fileName, char *formatString,
			    Tk_PhotoHandle imageHandle, int destX, int destY,
			    int width, int height, int srcX, int srcY));
static int		FileWriteJPG _ANSI_ARGS_((Tcl_Interp *interp,
			    char *fileName, char *formatString,
			    Tk_PhotoImageBlock *blockPtr));

#undef tkImgFmtJPG
Tk_PhotoImageFormat tkImgFmtJPG = {
    "JPEG",			/* name */
    FileMatchJPG,		/* fileMatchProc */
    NULL,			/* stringMatchProc */
    FileReadJPG,		/* fileReadProc */
    NULL,			/* stringReadProc */
    FileWriteJPG,		/* fileWriteProc */
    NULL,			/* stringWriteProc */
};

/*
 * Prototypes for local procedures defined in this file:
 */

struct my_error_mgr {
  struct jpeg_error_mgr pub;	/* "public" fields */
  jmp_buf setjmp_buffer;	/* for return to caller */
  Tcl_Interp *interp;		/* Where to report errors */
};

typedef struct my_error_mgr * my_error_ptr;

METHODDEF void
my_output_message(j_common_ptr cinfo)
{
 my_error_ptr myerr = (my_error_ptr) cinfo->err;
 if (myerr->interp)
  {
   char buffer[JMSG_LENGTH_MAX];                      
   (*cinfo->err->format_message) (cinfo, buffer);     
   Tcl_SetResult(myerr->interp, buffer, TCL_VOLATILE);
  }
}

/*
 * Here's the routine that will replace the standard error_exit method:
 */

METHODDEF void
my_error_exit (j_common_ptr cinfo)
{
  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
  my_error_ptr myerr = (my_error_ptr) cinfo->err;
  /* Return control to the setjmp point */
  longjmp(myerr->setjmp_buffer, 1);
}

static int ReadJPGFileHeader _ANSI_ARGS_((FILE *f, 
                                          struct jpeg_decompress_struct *cinfo,
                                          struct my_error_mgr *jerr));

/*
 *----------------------------------------------------------------------
 *
 * FileMatchJPG --
 *
 *	This procedure is invoked by the photo image type to see if
 *	a file contains image data in JPG format.
 *
 * Results:
 *	The return value is >0 if the first characters in file "f" look
 *	like JPG data, and 0 otherwise.
 *
 * Side effects:
 *	The access position in f may change.
 *
 *----------------------------------------------------------------------
 */

static int
FileMatchJPG(f, fileName, formatString, widthPtr, heightPtr)
    FILE *f;			/* The image file, open for reading. */
    char *fileName;		/* The name of the image file. */
    char *formatString;		/* User-specified format string, or NULL. */
    int *widthPtr, *heightPtr;	/* The dimensions of the image are
				 * returned here if the file is a valid
				 * raw JPG file. */
{
 struct jpeg_decompress_struct cinfo;
 struct my_error_mgr jerr;
 int code ;
 jerr.interp = NULL; 
 if ((code = ReadJPGFileHeader(f, &cinfo, &jerr)))
  {
   /* Fill in 'natural' width and height from cinfo */
   *widthPtr  = cinfo.image_width;
   *heightPtr = cinfo.image_height;
  }
 jpeg_destroy_decompress(&cinfo);
 return code;
}

/*
 *----------------------------------------------------------------------
 *
 * FileReadJPG --
 *
 *	This procedure is called by the photo image type to read
 *	JPG format data from a file and write it into a given
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
FileReadJPG(interp, f, fileName, formatString, imageHandle, destX, destY,
	width, height, srcX, srcY)
    Tcl_Interp *interp;		/* Interpreter to use for reporting errors. */
    FILE *f;			/* The image file, open for reading. */
    char *fileName;		/* The name of the image file. */
    char *formatString;		/* User-specified format string, or NULL. */
    Tk_PhotoHandle imageHandle;	/* The photo image to write into. */
    int destX, destY;		/* Coordinates of top-left pixel in
				 * photo image to be written to. */
    int width, height;		/* Dimensions of block of photo image to
				 * be written to. */
    int srcX, srcY;		/* Coordinates of top-left pixel to be used
				 * in image being read. */
{
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    Tk_PhotoImageBlock block;
    volatile int y = destY;

    /* More stuff */
    JSAMPARRAY buffer;		/* Output row buffer */
    int row_stride;		/* physical row width in output buffer */

    jerr.interp = interp; 

    if (!ReadJPGFileHeader(f, &cinfo, &jerr)) {
        (*cinfo.err->output_message) ((j_common_ptr) &cinfo);
        Tcl_AppendResult(interp," from \"",fileName,"\"",NULL); 
	return TCL_ERROR;
    }

  if (setjmp(jerr.setjmp_buffer)) {
    /* If we get here, the JPEG code has signaled an error.
     * We need to clean up the JPEG object, close the input file, and return.
     */
    jpeg_destroy_decompress(&cinfo);
    return TCL_ERROR;
  }

  /* Step 4: set parameters for decompression */

  /* In this example, we don't need to change any of the defaults set by
   * jpeg_read_header(), so we do nothing here.
   */

  /* Step 5: Start decompressor */

  jpeg_start_decompress(&cinfo);

  /* We may need to do some setup of our own at this point before reading
   * the data.  After jpeg_start_decompress() we have the correct scaled
   * output image dimensions available, as well as the output colormap
   * if we asked for color quantization.
   */
  Tk_PhotoExpand(imageHandle, destX + cinfo.output_width, 
                 destY + cinfo.output_height);
  block.width     = cinfo.output_width;
  block.height    = 1;
  block.pixelSize = cinfo.output_components;;
  block.pitch     = block.pixelSize * cinfo.output_width;
  block.offset[0] = 0;
  if (block.pixelSize == 3)
   {
    block.offset[1] = 1;
    block.offset[2] = 2;
   }
  else
   {
    /* Grey scale */
    block.offset[1] = 0;
    block.offset[2] = 0;
   }

  /*
   * In this example, we need to make an output work buffer of the right size.
   */ 
  /* JSAMPLEs per row in output buffer */
  row_stride = cinfo.output_width * cinfo.output_components;
  /* Make a one-row-high sample array that will go away when done with image */

  row_stride = cinfo.output_width * cinfo.output_components;
  buffer = (*cinfo.mem->alloc_sarray)
		((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

  /* Step 6: while (scan lines remain to be read) */
  /*           jpeg_read_scanlines(...); */

  /* Here we use the library's state variable cinfo.output_scanline as the
   * loop counter, so that we don't have to keep track ourselves.
   */
 
  while (cinfo.output_scanline < cinfo.output_height) 
   {
    jpeg_read_scanlines(&cinfo, buffer, 1);
    block.pixelPtr = (unsigned char *)(buffer[0]);
    Tk_PhotoPutBlock(imageHandle, &block, destX, y++, 
                     width, 1);
   }

  /* Step 7: Finish decompression */

  (void) jpeg_finish_decompress(&cinfo);
  /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

  /* Step 8: Release JPEG decompression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_decompress(&cinfo);

  /* After finish_decompress, we can close the input file.
   * Here we postpone it until after no more JPEG errors are possible,
   * so as to simplify the setjmp error logic above.  (Actually, I don't
   * think that jpeg_destroy can do an error exit, but why assume anything...)
   */

  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * FileWriteJPG --
 *
 *	This procedure is invoked to write image data to a file in JPG
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
FileWriteJPG(interp, fileName, formatString, blockPtr)
    Tcl_Interp *interp;
    char *fileName;
    char *formatString;
    Tk_PhotoImageBlock *blockPtr;
{
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * ReadJPGFileHeader --
 *
 *	This procedure reads the JPG header from the beginning of a
 *	JPG file and returns information from the header.
 *
 * Results:
 *	The return value is PGM if file "f" appears to start with
 *	a valid PGM header, JPG if "f" appears to start with a valid
 *      JPG header, and 0 otherwise.  If the header is valid,
 *	then *widthPtr and *heightPtr are modified to hold the
 *	dimensions of the image and *maxIntensityPtr is modified to
 *	hold the value of a "fully on" intensity value.
 *
 * Side effects:
 *	The access position in f advances.
 *
 *----------------------------------------------------------------------
 */

static int
ReadJPGFileHeader(f, cinfo, jerr)
    FILE *f;			/* Image file to read the header from */
    struct jpeg_decompress_struct *cinfo;
    struct my_error_mgr *jerr;
{
  /* This struct contains the JPEG decompression parameters and pointers to
   * working space (which is allocated as needed by the JPEG library).
   */
  /* We use our private extension JPEG error handler. */

  /* Step 1: allocate and initialize JPEG decompression object */

  /* We set up the normal JPEG error routines, then override error_exit. */
  cinfo->err = jpeg_std_error(&jerr->pub);
  jerr->pub.error_exit = my_error_exit;
  jerr->pub.output_message = my_output_message;
  /* Establish the setjmp return context for my_error_exit to use. */
  if (setjmp(jerr->setjmp_buffer)) {
    /* If we get here, the JPEG code has signaled an error.
     * We need to clean up the JPEG object, close the input file, and return.
     */
    jpeg_destroy_decompress(cinfo);
    return 0;
  }
  /* Now we can initialize the JPEG decompression object. */
  jpeg_create_decompress(cinfo);

  /* Step 2: specify data source (eg, a file) */

  jpeg_stdio_src(cinfo, f);

  /* Step 3: read file parameters with jpeg_read_header() */

  (void) jpeg_read_header(cinfo, TRUE);
  /* We can ignore the return value from jpeg_read_header since
   *   (a) suspension is not possible with the stdio data source, and
   *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   * See libjpeg.doc for more info.
   */


  return 1;
}
