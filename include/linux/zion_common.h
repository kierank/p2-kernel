#ifndef __ZION_COMMON_H__
#define __ZION_COMMON_H__

struct zion_config_byte
{
  int where;
  unsigned char val;
};

struct zion_config_word
{
  int where;
  unsigned short val;
};

struct zion_config_dword
{
  int where;
  unsigned int val;
};

struct zion_buf
{
  unsigned long addr;
  unsigned long size;
  void *buf;
  char access_type;
  int dma_ch;
};

/* MBus Register Read/Write */
#define ZION_MBUS_READ_CONFIG_BYTE _IOR(ZION_MAGIC,6, struct zion_config_byte)
#define ZION_MBUS_READ_CONFIG_WORD _IOR(ZION_MAGIC,7, struct zion_config_word)
#define ZION_MBUS_READ_CONFIG_DWORD _IOR(ZION_MAGIC,8, struct zion_config_dword)
#define ZION_MBUS_WRITE_CONFIG_BYTE _IOW(ZION_MAGIC,9, struct zion_config_byte)
#define ZION_MBUS_WRITE_CONFIG_WORD _IOW(ZION_MAGIC,10, struct zion_config_word)
#define ZION_MBUS_WRITE_CONFIG_DWORD _IOW(ZION_MAGIC,11, struct zion_config_dword)

/* Work-RAM Read/Write */
#define ZION_WRAM_READ _IOR(ZION_MAGIC,12, struct zion_buf)
#define ZION_WRAM_WRITE _IOW(ZION_MAGIC,13, struct zion_buf)

/* Wait Interrupts */
#define ZION_WAIT_INTERRUPT _IOR(ZION_MAGIC, 14, struct ZION_Interrupt_Bits)
#define ZION_WAKE_THREADS_UP _IO(ZION_MAGIC, 15)
#define ZION_SET_ENABLE_BITS _IOW(ZION_MAGIC, 16, struct ZION_Interrupt_Bits)
#define ZION_GET_ENABLE_BITS _IOR(ZION_MAGIC, 17, struct ZION_Interrupt_Bits)
#define ZION_SET_TIMEOUT     _IO(ZION_MAGIC, 18)

/* getting ZION's revision */
#define ZION_GET_REVISION _IOR(ZION_MAGIC, 255, unsigned short)

#ifdef __KERNEL__

static inline unsigned long memcpy_toio_dword(unsigned long to, const void *from, unsigned long count)
{
  unsigned long left_count = count;
  u32 *from_pt, *to_pt;

  if(count%sizeof(u32))
    {
      PERROR("Invalid Size.\n");
      return 0;
    }

  from_pt = (u32 *)from;
  to_pt = (u32 *)to;

  while(left_count)
    {
      left_count-=sizeof(u32);
      iowrite32(cpu_to_le32(*from_pt), (void *)to_pt);
      from_pt++;
      to_pt++;
    }

  return count;
}

static inline unsigned long  memcpy_fromio_dword(void *to, unsigned long from, unsigned long count)
{
  unsigned long  left_count = count;
  u32 *from_pt, *to_pt;

  if(count%sizeof(u32))
    {
      PERROR("Invalid Size.\n");
      return 0;
    }

  from_pt = (u32 *)from;
  to_pt = (u32 *)to;

  while(left_count)
    {
      left_count-=sizeof(u32);
      *to_pt = le32_to_cpu(ioread32((void*)from_pt));
      from_pt++;
      to_pt++;
    }

  return count;
}

static inline unsigned long memcpy_toio_word(unsigned long to, const void *from, unsigned long count)
{
  unsigned long left_count = count;
  u16 *from_pt, *to_pt;

  if(count%sizeof(u16))
    {
      PERROR("Invalid Size.\n");
      return 0;
    }

  from_pt = (u16 *)from;
  to_pt = (u16 *)to;

  while(left_count)
    {
      left_count-=sizeof(u16);
      iowrite16(cpu_to_le16(*from_pt), (void *)to_pt);
      from_pt++;
      to_pt++;
    }

  return count;
}

static inline unsigned long  memcpy_fromio_word(void *to, unsigned long from, unsigned long count)
{
  unsigned long  left_count = count;
  u16 *from_pt, *to_pt;

  if(count%sizeof(u16))
    {
      PERROR("Invalid Size.\n");
      return 0;
    }

  from_pt = (u16 *)from;
  to_pt = (u16 *)to;

  while(left_count)
    {
      left_count-=sizeof(u16);
      *to_pt = le16_to_cpu(ioread16((void *)from_pt));
      from_pt++;
      to_pt++;
    }

  return count;
}

static inline unsigned long memcpy_toio_byte(unsigned long to, const void *from, unsigned long count)
{
  unsigned long  left_count = count;
  char *from_pt, *to_pt;

  from_pt = (char *)from;
  to_pt = (char *)to;

  while(left_count)
    {
      left_count--;
      writeb(*from_pt, (void *)to_pt);
      from_pt++;
      to_pt++;
    }

  return count;
}

static inline unsigned long  memcpy_fromio_byte(void *to, unsigned long from, unsigned long count)
{
  unsigned long  left_count = count;
  char *from_pt, *to_pt;

  from_pt = (char *)from;
  to_pt = (char *)to;

  while(left_count)
    {
      left_count--;
      *to_pt = readb((void *)from_pt);
      to_pt++;
      from_pt++;
    }

  return count;
}

int zion_common_init(void);
void zion_common_exit(void);

#endif  /* __KERNEL__ */

#endif  /* __ZION_COMMON_H__ */
