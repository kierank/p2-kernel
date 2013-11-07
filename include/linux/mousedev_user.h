/*****************************************************************************
 *  linux/include/linux/mousedev_user.h
 *
 *   Header file of mousedev driver for users
 *     
 *     Copyright (C) 2009 Panasonic Co.,LTD
 *     All Rights Reserved.
 *
 *****************************************************************************/
/* $Id: mousedev_user.h 10412 2010-11-15 09:23:11Z Noguchi Isao $ */

#ifndef _LINUX_MOUSEDEV_USER_H_
#define _LINUX_MOUSEDEV_USER_H_

/*
 * structures
 */

/* sttructure for pseude-event to move */
struct mousedev_pevent_move {
    int dx;                     /* delta-X */
    int dy;                     /* delta-Y */
    int dz;                     /* delta-Z */
}; 

/* sttructure for pseude-event to button */
struct mousedev_pevent_button {
    int btn;                     /* button number(0-7) */
    int evt;                     /* non-0: push, 0: release */
}; 



/*
 * macro
 */

/** ioctl commands **/
#define MOUSEDEV_IOC_PEVENT_MOVE	0x01
#define MOUSEDEV_IOC_PEVENT_BTN     0x02
#define MOUSEDEV_IOC_EMUL_MODE		0x03
#define MOUSEDEV_IOC_JOGMODE		MOUSEDEV_IOC_EMUL_MODE


#define MOUSEDEV_EMUL_MODE_PS2  0
#define MOUSEDEV_EMUL_MODE_JOG  1
#define MOUSEDEV_EMUL_MODE_IMPS 3
#define MOUSEDEV_EMUL_MODE_EXPS 4


#endif /* _LINUX_MOUSEDEV_USER_H_ */

