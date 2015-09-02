/*
 * "$Id: rastertolabel.c 6820 2007-08-20 21:15:28Z mike $"
 *
 *   Label printer filter for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 2007 by Apple Inc.
 *   Copyright 2001-2007 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 *   which should have been included with this file.  If this file is
 *   file is missing or damaged, see the license at "http://www.cups.org/".
 *
 *   This file is subject to the Apple OS-Developed Software exception.
 *
 * Contents:
 *
 *   StartPage()    - Start a page of graphics.
 *   EndPage()      - Finish a page of graphics.
 *   CancelJob()    - Cancel the current job...
 *   OutputLine()   - Output a line of graphics.
 *   PCLCompress()  - Output a PCL (mode 3) compressed line.
 *   ZPLCompress()  - Output a run-length compression sequence.
 *   main()         - Main entry and processing of driver.
 */

/*
 * Include necessary headers...
 */

#include <cups/cups.h>
#include <cups/string.h>
#include <cups/i18n.h>
#include "raster.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>


/*
 * This driver filter currently supports the Zebra p330i card printer.
 */

/*
 * Model number constants...
 */

#define ZEBRA_EPCL 0		/* Zebra EPCL-based printers */

/*
 * Globals...
 */

unsigned char	*Buffer;		/* Output buffer */
unsigned char	*CompBuffer;		/* Compression buffer */
unsigned char	*LastBuffer;		/* Last buffer */
int		LastSet;		/* Number of repeat characters */
int		ModelNumber,		/* cupsModelNumber attribute */
		Page,			/* Current page */
		Feed,			/* Number of lines to skip */
		Canceled;		/* Non-zero if job is canceled */


/*
 * Prototypes...
 */

void	StartPage(ppd_file_t *ppd, cups_page_header_t *header);
void	EndPage(ppd_file_t *ppd, cups_page_header_t *header);
void	CancelJob(int sig);
void	OutputLine(ppd_file_t *ppd, cups_page_header_t *header, int y);
void	PCLCompress(unsigned char *line, int length);
void	ZPLCompress(char repeat_char, int repeat_count);

/*
 * 'StartPage()' - Start a page of graphics.
 */

void
StartPage(ppd_file_t         *ppd,	/* I - PPD file */
          cups_page_header_t *header)	/* I - Page header */
{
  ppd_choice_t	*choice;		/* Marked choice */
  int		length;			/* Actual label length */
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


 /*
  * Show page device dictionary...
  */

  fprintf(stderr, "DEBUG: StartPage...\n");
  fprintf(stderr, "DEBUG: MediaClass = \"%s\"\n", header->MediaClass);
  fprintf(stderr, "DEBUG: MediaColor = \"%s\"\n", header->MediaColor);
  fprintf(stderr, "DEBUG: MediaType = \"%s\"\n", header->MediaType);
  fprintf(stderr, "DEBUG: OutputType = \"%s\"\n", header->OutputType);

  fprintf(stderr, "DEBUG: AdvanceDistance = %d\n", header->AdvanceDistance);
  fprintf(stderr, "DEBUG: AdvanceMedia = %d\n", header->AdvanceMedia);
  fprintf(stderr, "DEBUG: Collate = %d\n", header->Collate);
  fprintf(stderr, "DEBUG: CutMedia = %d\n", header->CutMedia);
  fprintf(stderr, "DEBUG: Duplex = %d\n", header->Duplex);
  fprintf(stderr, "DEBUG: HWResolution = [ %d %d ]\n", header->HWResolution[0],
          header->HWResolution[1]);
  fprintf(stderr, "DEBUG: ImagingBoundingBox = [ %d %d %d %d ]\n",
          header->ImagingBoundingBox[0], header->ImagingBoundingBox[1],
          header->ImagingBoundingBox[2], header->ImagingBoundingBox[3]);
  fprintf(stderr, "DEBUG: InsertSheet = %d\n", header->InsertSheet);
  fprintf(stderr, "DEBUG: Jog = %d\n", header->Jog);
  fprintf(stderr, "DEBUG: LeadingEdge = %d\n", header->LeadingEdge);
  fprintf(stderr, "DEBUG: Margins = [ %d %d ]\n", header->Margins[0],
          header->Margins[1]);
  fprintf(stderr, "DEBUG: ManualFeed = %d\n", header->ManualFeed);
  fprintf(stderr, "DEBUG: MediaPosition = %d\n", header->MediaPosition);
  fprintf(stderr, "DEBUG: MediaWeight = %d\n", header->MediaWeight);
  fprintf(stderr, "DEBUG: MirrorPrint = %d\n", header->MirrorPrint);
  fprintf(stderr, "DEBUG: NegativePrint = %d\n", header->NegativePrint);
  fprintf(stderr, "DEBUG: NumCopies = %d\n", header->NumCopies);
  fprintf(stderr, "DEBUG: Orientation = %d\n", header->Orientation);
  fprintf(stderr, "DEBUG: OutputFaceUp = %d\n", header->OutputFaceUp);
  fprintf(stderr, "DEBUG: PageSize = [ %d %d ]\n", header->PageSize[0],
          header->PageSize[1]);
  fprintf(stderr, "DEBUG: Separations = %d\n", header->Separations);
  fprintf(stderr, "DEBUG: TraySwitch = %d\n", header->TraySwitch);
  fprintf(stderr, "DEBUG: Tumble = %d\n", header->Tumble);
  fprintf(stderr, "DEBUG: cupsWidth = %d\n", header->cupsWidth);
  fprintf(stderr, "DEBUG: cupsHeight = %d\n", header->cupsHeight);
  fprintf(stderr, "DEBUG: cupsMediaType = %d\n", header->cupsMediaType);
  fprintf(stderr, "DEBUG: cupsBitsPerColor = %d\n", header->cupsBitsPerColor);
  fprintf(stderr, "DEBUG: cupsBitsPerPixel = %d\n", header->cupsBitsPerPixel);
  fprintf(stderr, "DEBUG: cupsBytesPerLine = %d\n", header->cupsBytesPerLine);
  fprintf(stderr, "DEBUG: cupsColorOrder = %d\n", header->cupsColorOrder);
  fprintf(stderr, "DEBUG: cupsColorSpace = %d\n", header->cupsColorSpace);
  fprintf(stderr, "DEBUG: cupsCompression = %d\n", header->cupsCompression);
  fprintf(stderr, "DEBUG: cupsRowCount = %d\n", header->cupsRowCount);
  fprintf(stderr, "DEBUG: cupsRowFeed = %d\n", header->cupsRowFeed);
  fprintf(stderr, "DEBUG: cupsRowStep = %d\n", header->cupsRowStep);

 /*
  * Register a signal handler to eject the current page if the
  * job is canceled.
  */

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, CancelJob);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = CancelJob;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, CancelJob);
#endif /* HAVE_SIGSET */

  /* Clear varnish buffer */
  fprintf(stderr, "vF\n");
  fputs("\x1BvF\x0D");

  /* Clear black buffer */
  fprintf(stderr, "F\n");
  puts("\x1BF\x0D");

  /* clear color buffers */
  fprintf(stderr, "$F\n");
  puts("\x1B$F\x0D");

  CompBuffer = malloc(2 * header->cupsBytesPerLine + 1);
  LastBuffer = malloc(header->cupsBytesPerLine);
  LastSet    = 0;

  Buffer = malloc(header->cupsBytesPerLine);
  Feed   = 0;
}


/*
 * 'EndPage()' - Finish a page of graphics.
 */

void
EndPage(ppd_file_t *ppd,		/* I - PPD file */
        cups_page_header_t *header)	/* I - Page header */
{
  int		val;			/* Option value */
  ppd_choice_t	*choice;		/* Marked choice */
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


  /*
   * Print the appropriate color
   */

  if ((Page % 4) == 3) {
    fprintf(stderr, "Outputting page %d", Page / 4 + 1);
    fprintf(stderr, "\x1BM %d IS 0[IS 1[IS 2[I[IV 1\x0D", header->NumCopies);
    printf("\x1BM %d IS 0[IS 1[IS 2[I[IV 1\x0D", header->NumCopies);
  }

  fflush(stdout);

 /*
  * Unregister the signal handler...
  */

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, SIG_IGN);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = SIG_IGN;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, SIG_IGN);
#endif /* HAVE_SIGSET */

 /*
  * Free memory...
  */

  free(Buffer);
}


/*
 * 'CancelJob()' - Cancel the current job...
 */

void
CancelJob(int sig)			/* I - Signal */
{
 /*
  * Tell the main loop to stop...
  */

  (void)sig;

  Canceled = 1;
}


/*
 * 'OutputLine()' - Output a line of graphics...
 */

void
OutputLine(ppd_file_t         *ppd,	/* I - PPD file */
           cups_page_header_t *header,	/* I - Page header */
           int                y)	/* I - Line number */
{
  int		i;			/* Looping var */
  unsigned char	*ptr;			/* Pointer into buffer */
  unsigned char	*compptr;		/* Pointer into compression buffer */
  char		repeat_char;		/* Repeated character */
  int		repeat_count;		/* Number of repeated characters */
  static const char *hex = "0123456789ABCDEF";
					/* Hex digits */

  switch (ModelNumber) {
    case ZEBRA_EPL_LINE :
        printf("\033g%03d", header->cupsBytesPerLine);
	fwrite(Buffer, 1, header->cupsBytesPerLine, stdout);
	fflush(stdout);
        break;

    case ZEBRA_EPL_PAGE :
        if (Buffer[0] || memcmp(Buffer, Buffer + 1, header->cupsBytesPerLine))
	{
          printf("GW0,%d,%d,1\n", y, header->cupsBytesPerLine);
	  for (i = header->cupsBytesPerLine, ptr = Buffer; i > 0; i --, ptr ++)
	    putchar(~*ptr);
	  putchar('\n');
	  fflush(stdout);
	}
        break;

    case ZEBRA_ZPL :
       /*
	* Determine if this row is the same as the previous line.
        * If so, output a ':' and return...
        */

        if (LastSet)
	{
	  if (!memcmp(Buffer, LastBuffer, header->cupsBytesPerLine))
	  {
	    putchar(':');
	    return;
	  }
	}

       /*
        * Convert the line to hex digits...
	*/

	for (ptr = Buffer, compptr = CompBuffer, i = header->cupsBytesPerLine;
	     i > 0;
	     i --, ptr ++)
        {
	  *compptr++ = hex[*ptr >> 4];
	  *compptr++ = hex[*ptr & 15];
	}

        *compptr = '\0';

       /*
        * Run-length compress the graphics...
	*/

	for (compptr = CompBuffer + 1, repeat_char = CompBuffer[0], repeat_count = 1;
	     *compptr;
	     compptr ++)
	  if (*compptr == repeat_char)
	    repeat_count ++;
	  else
	  {
	    ZPLCompress(repeat_char, repeat_count);
	    repeat_char  = *compptr;
	    repeat_count = 1;
	  }

        if (repeat_char == '0')
	{
	 /*
	  * Handle 0's on the end of the line...
	  */

	  if (repeat_count & 1)
	  {
	    repeat_count --;
	    putchar('0');
	  }

          if (repeat_count > 0)
	    putchar(',');
	}
	else
	  ZPLCompress(repeat_char, repeat_count);

	fflush(stdout);

       /*
        * Save this line for the next round...
	*/

	memcpy(LastBuffer, Buffer, header->cupsBytesPerLine);
	LastSet = 1;
        break;

    case ZEBRA_CPCL :
        if (Buffer[0] || memcmp(Buffer, Buffer + 1, header->cupsBytesPerLine))
	{
	  printf("CG %u 1 0 %d ", header->cupsBytesPerLine, y);
          fwrite(Buffer, 1, header->cupsBytesPerLine, stdout);
	  puts("\r");
	  fflush(stdout);
	}
	break;

    case INTELLITECH_PCL :
	if (Buffer[0] ||
            memcmp(Buffer, Buffer + 1, header->cupsBytesPerLine - 1))
        {
	  if (Feed)
	  {
	    printf("\033*b%dY", Feed);
	    Feed    = 0;
	    LastSet = 0;
	  }

          PCLCompress(Buffer, header->cupsBytesPerLine);
	}
	else
	  Feed ++;
        break;
  }
}

void BitmapCompress(char *input, size_t input_len, char *output) {
  char last = '\0', cur = '\0';

  int same_count = 0;
  int diff_count = 0;

  int off = 0;

  int i;

  for (i = 0; i < input_len; i++) {
    cur = input[i];
    if (last == '\0' || cur == last) {
      if (diff_count) {
	output[off++] = 128 | (diff_count-1);
	memcpy(output + off, input + i - diff_count + 1, diff_count-2);
	off += diff_count-2;
	diff_count = 0;
      }

      if (same_count == 127) {
	output[off++] = FLAG_RPT | same_count;
	output[off++] = last;
	same_count = 0;
      }

      same_count++;
    } else {
      if (same_count) {
	output[off++] = FLAG_RPT | same_count;
	output[off++] = last;
	same_count = 0;
      }

      if (diff_count == 31) {
	output[off++] = diff_count;
	memcpy(output + off, input + i - diff_count, diff_count);
	off += diff_count;
	diff_count = 0;
      }
    }

    last = cur;
  }

  if (diff_count == 31) {
    output[off++] = diff_count;
    memcpy(output + off, input + i - diff_count, diff_count);
    off += diff_count;
    diff_count = 0;
  }

  if (same_count) {
    output[off++] = FLAG_RPT | same_count;
    output[off++] = last;
    same_count = 0;
  }
}
/*
 * 'main()' - Main entry and processing of driver.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int			fd;		/* File descriptor */
  cups_raster_t		*ras;		/* Raster stream for printing */
  cups_page_header_t	header;		/* Page header from file */
  int			y;		/* Current line */
  ppd_file_t		*ppd;		/* PPD file */
  int			num_options;	/* Number of options */
  cups_option_t		*options;	/* Options */


 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Check command-line...
  */

  if (argc < 6 || argc > 7)
  {
   /*
    * We don't have the correct number of arguments; write an error message
    * and return.
    */

    fprintf(stderr, _("Usage: %s job-id user title copies options [file]\n"),
            argv[0]);
    return (1);
  }

 /*
  * Open the page stream...
  */

  if (argc == 7)
  {
    if ((fd = open(argv[6], O_RDONLY)) == -1)
    {
      perror("ERROR: Unable to open raster file - ");
      sleep(1);
      return (1);
    }
  }
  else
    fd = 0;

  ras = cupsRasterOpen(fd, CUPS_RASTER_READ);

 /*
  * Open the PPD file and apply options...
  */

  num_options = cupsParseOptions(argv[5], 0, &options);

  if ((ppd = ppdOpenFile(getenv("PPD"))) != NULL)
  {
    ppdMarkDefaults(ppd);
    cupsMarkOptions(ppd, num_options, options);
  }

 /*
  * Clear any cards left in the print area, if present
  */

  puts("\x1BMC\x0D");

 /*
  * Process pages as needed...
  */

  Page      = 0;
  Canceled = 0;

  while (cupsRasterReadHeader(ras, &header))
  {
   /*
    * Write a status message with the page number and number of copies.
    */

    Page ++;

    fprintf(stderr, "PAGE: %d 1\n", Page);

   /*
    * Start the page...
    */

    StartPage(ppd, &header);

   /*
    * Loop for each line on the page...
    */

    for (y = 0; y < header.cupsHeight && !Canceled; y ++)
    {
     /*
      * Let the user know how far we have progressed...
      */

      if ((y & 15) == 0)
        fprintf(stderr, _("INFO: Printing page %d, %d%% complete...\n"), Page,
	        100 * y / header.cupsHeight);

     /*
      * Read a line of graphics if we need more
      */

      if (cupsRasterReadPixels(ras, Buffer, header.cupsBytesPerLine) < 1)
        break;

      /*
       * Compress data as we get it
       */

      /*
       * send it over based on the color?
       */

     /*
      * Write it to the printer...
      */

      OutputLine(ppd, &header, y);
    }

   /*
    * Eject the page...
    */

    EndPage(ppd, &header);

    if (Canceled)
      break;
  }

 /*
  * Close the raster stream...
  */

  cupsRasterClose(ras);
  if (fd != 0)
    close(fd);

 /*
  * Close the PPD file and free the options...
  */

  ppdClose(ppd);
  cupsFreeOptions(num_options, options);

 /*
  * If no pages were printed, send an error message...
  */

  if (Page == 0)
    fputs(_("ERROR: No pages found!\n"), stderr);
  else
    fputs(_("INFO: Ready to print.\n"), stderr);

  return (Page == 0);
}


/*
 * End of "$Id: rastertolabel.c 6820 2007-08-20 21:15:28Z mike $".
 */
