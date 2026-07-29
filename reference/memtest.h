/**********************************************************************
 *
 * Filename:    memtest.h
 * 
 * Description: Memory-testing module API.
 *
 * Notes:       The memory tests can be easily ported to systems with
 *              different data bus widths by redefining 'datum' type.
 *
 * 
 * Copyright (c) 2000 by Michael Barr.  This software is placed into
 * the public domain and may be used for any purpose.  However, this
 * notice must not be changed or removed and no warranty is either
 * expressed or implied by its publication or distribution.
 **********************************************************************/

#ifndef _memtest_h
#define _memtest_h

/*
 * Set the data bus width.
 */
typedef unsigned char datum;

/*
 * Function prototypes.
 */
datum   memTestDataBus();
datum * memTestAddressBus();
datum * memTestDevice();

#endif /* _memtest_h */
