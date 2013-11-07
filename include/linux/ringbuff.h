/*
 *	include/linux/ringbuff.h
 */
/* $Id: ringbuff.h 13117 2011-03-10 04:38:15Z Noguchi Isao $ */

#ifndef	_LINUX_RINGBUFF_H_
#define	_LINUX_RINGBUFF_H_

#include <linux/kernel.h>
#include <linux/err.h>

struct ringbuff {
	unsigned int	Rptr;
	unsigned int	Wptr;
	unsigned int	Num;
	unsigned int	MaxNum;
    unsigned int    EntrySize;
	unsigned char   *Buff;
};

struct ringbuff_data {
    unsigned int ptr;
    void *entry;
    void *private;
};


/*	0: not empty, 1: empty	*/
#define ringbuff_empty(fp)	(((fp)->Num<=0)?1:0)
/*	0: not full, 1: full	*/
#define ringbuff_full(fp)	(((fp)->Num>=(fp)->MaxNum)?1:0)
/*	Refer Data Number	*/
#define	ringbuff_num(fp)	((fp)->Num)


static inline void ringbuff_clear(struct ringbuff *const fp)
{
	fp->Rptr = fp->Wptr = 0;
	fp->Num = 0;
}

extern int ringbuff_get_and_delete(struct ringbuff *const fp, void *data);
extern int ringbuff_put(struct ringbuff *const fp, const void *data);
extern int ringbuff_foreach (struct ringbuff *const fp, 
                             int (*func)(struct ringbuff *const fp, struct ringbuff_data *data),
                             void *private,
                             int force );
extern struct ringbuff *ringbuff_create(const unsigned int MaxNum, const unsigned int EntrySize);
extern void ringbuff_destroy(struct ringbuff *const fp);

#endif  /* _LINUX_RINGBUFF_H_ */
