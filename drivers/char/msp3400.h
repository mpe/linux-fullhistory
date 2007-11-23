#ifndef MSP3400_H
#define MSP3400_H

/* ---------------------------------------------------------------------- */

#define MSP_SET_TVNORM     _IOW('m',1,int)  /* TV mode + PAL/SECAM/NTSC  */
#define MSP_SET_RADIO      _IO('m',2)       /* Radio mode                */
#define MSP_NEWCHANNEL     _IO('m',3)       /* indicate new channel      */

#define MSP_GET_VOLUME     _IOR('m',4,int)
#define MSP_SET_VOLUME     _IOW('m',5,int)

#define MSP_GET_STEREO     _IOR('m',6,int)
#define MSP_SET_STEREO     _IOW('m',7,int)

#define MSP_GET_DC         _IOW('m',8,int)

#define MSP_GET_BASS       _IOR('m', 9,int)
#define MSP_SET_BASS       _IOW('m',10,int)
#define MSP_GET_TREBLE     _IOR('m',11,int)
#define MSP_SET_TREBLE     _IOW('m',12,int)

#define MSP_GET_UNIT	   _IOR('m',13,int)

#endif /* MSP3400_H */
