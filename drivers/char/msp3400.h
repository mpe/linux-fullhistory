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

#endif /* MSP3400_H */
