/*
 *	lib/ringbuff.c
 */
/* $Id: ringbuff.c 13117 2011-03-10 04:38:15Z Noguchi Isao $ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/err.h>

#include <linux/ringbuff.h>

int ringbuff_get_and_delete(struct ringbuff *const fp, void *data)
{
    unsigned char *p = &fp->Buff[(fp->Rptr++) * (fp->EntrySize)];
    if(fp->Num==0)
        return -ENOENT;
    if(data)
        memcpy(data,(const void*)p,fp->EntrySize);
	if(fp->Rptr>=fp->MaxNum)
		fp->Rptr = 0;
    fp->Num--;
    return 0;
}
EXPORT_SYMBOL(ringbuff_get_and_delete);


int ringbuff_put(struct ringbuff *const fp, const void *data)
{
    unsigned char *p = &fp->Buff[(fp->Wptr++) * (fp->EntrySize)];
    if(unlikely(!data))
        return -EINVAL;
    memcpy((void*)p,data,fp->EntrySize);
	if(fp->Wptr>=fp->MaxNum)
		fp->Wptr = 0;
    if(fp->Num<fp->MaxNum)
        fp->Num++;
    return 0;
}
EXPORT_SYMBOL(ringbuff_put);

int ringbuff_foreach (struct ringbuff *const fp, 
                      int (*func)(struct ringbuff *const fp, struct ringbuff_data *data),
                      void *private,
                      int force )
{
    unsigned int rptr;
    struct ringbuff_data data;
    unsigned int i;

    data.private=private;
    for(i=0, rptr=fp->Rptr; i<fp->Num; i++) {
        int retval;
        data.ptr=rptr;
        data.entry = (void*)&fp->Buff[(rptr++) * (fp->EntrySize)];
        retval=(*func)(fp,&data);
        if(retval<0 && !force)
            return retval;
        if(rptr>=fp->MaxNum)
            rptr = 0;
    }

    return 0;
}
EXPORT_SYMBOL(ringbuff_foreach);

struct ringbuff *ringbuff_create(const unsigned int MaxNum, const unsigned int EntrySize)
{
    int retval = 0;
    struct ringbuff *fp=NULL;

    if (unlikely(!MaxNum||!EntrySize)) {
        retval = -EINVAL;
        goto failed;
    }

    fp=(struct ringbuff *)kzalloc(sizeof(struct ringbuff), GFP_KERNEL);
    if(!fp){
        retval = -ENOMEM;
        goto failed;
    }
    
	fp->MaxNum = MaxNum;
    fp->EntrySize = EntrySize;
	fp->Buff = (unsigned char *)kzalloc( (fp->MaxNum*fp->EntrySize), GFP_KERNEL);
    if (!fp->Buff) {
        retval = -ENOMEM;
        goto failed;
    }

    return fp;

 failed:
    if(fp){
        if(fp->Buff){
            kfree(fp->Buff);
            fp->Buff=NULL;
        }
        kfree(fp);
        fp=NULL;
    }
    return ERR_PTR(retval);
}
EXPORT_SYMBOL(ringbuff_create);

void ringbuff_destroy(struct ringbuff *const fp)
{
    if(fp){
        if(fp->Buff){
            kfree(fp->Buff);
            fp->Buff=NULL;
        }
        kfree(fp);
    }
}
EXPORT_SYMBOL(ringbuff_destroy);

