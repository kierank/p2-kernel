/*
 * scullp.h -- definitions for the scullp char module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 */

#include <linux/ioctl.h>

/* Total number of buffer	*/
#define DMABUF_NUM 2

/* Ioctl definitions  */
#define IOC_SCULLP_GETBUFLIST 100

typedef struct {
	void *		buf_kaddr;
	size_t		buf_size;
	loff_t		buf_offset;
} ScullP_IOC_buf_spec;

typedef struct {
	size_t		size;			/* size of this struct		*/
	int			version;		/* version of this struct	*/
	int			buf_num;		/* number of buffer array	*/
	ScullP_IOC_buf_spec	buf_list[DMABUF_NUM];
} ScullP_IOC_GETBUFLIST;

