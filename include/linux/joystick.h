#ifndef _LINUX_JOYSTICK_H
#define _LINUX_JOYSTICK_H

#define JS_RETURN sizeof(struct js_status)	/*number of bytes returned by js_read*/
#define JS_TRUE 1
#define JS_FALSE 0
#define JS_PORT 0x201		/*io port for joystick operations*/
#define JS_DEF_TIMEOUT 0x1300	/*default timeout value for js_read()*/
#define JS_DEF_CORR	0	/*default correction factor*/
#define JS_DEF_TIMELIMIT 10L	/*default data valid time =10 jiffies == 100ms*/
#define JS_X_0	0x01		/*bit mask for x-axis js0*/
#define JS_Y_0	0x02		/*bit mask for y-axis js0*/
#define JS_X_1	0x04		/*bit mask for x-axis js1*/
#define JS_Y_1	0x08		/*bit mask for y-axis js1*/
#define JS_MAX 2		/*Max number of joysticks*/
#define PIT_MODE 0x43		/*io port for timer 0*/
#define PIT_COUNTER_0 0x40	/*io port for timer 0*/
#define JSIOCSCAL 0x01		/*ioctl cmd to set joystick correction factor*/
#define JSIOCGCAL 0x02		/*ioctl cmd to get joystick correction factor*/
#define JSIOCSTIMEOUT 0x03	/*ioctl cmd to set maximum number of iterations
				  to wait for a timeout*/
#define JSIOCGTIMEOUT		0x04	/*as above, to get*/
#define JSIOCSTIMELIMIT	0x05	/*set data retention time*/
#define JSIOCGTIMELIMIT	0x06	/*get data retention time*/
#define JSIOCGCONFIG		0x07	/*get the whole js_data[minor] struct*/
#define JSIOCSCONFIG		0x08	/*set the whole js_data[minor] struct
						  except js_busy!*/

/*
 *	This union is used for the ioctl to set the scaling factor and to 
 *	return the current values for a joystick. 'buttons' is ignored on
 *	the ioctl call
 */

struct js_status 
{
	int buttons;
	int x;
	int y;
};

/*
 *	This struct is used for misc data about the joystick
 */

struct js_config 
{
	int js_timeout;		/*timeout*/
	int busy;		/*joystick is in use*/
	long js_expiretime;	/*Time when stick after which stick must be re-read*/
	long js_timelimit;	/*Max time before data is invalid*/
	struct js_status js_save;	/*last read data*/
	struct js_status js_corr;	/*correction factor*/
};

#define LATCH (1193180L/HZ)	/*initial timer 0 value*/
#define DELTA_TIME(X,Y) ((X)-(Y)+(((X)>=(Y))?0:LATCH))

extern int joystick_init(void);

#endif /* _LINUX_JOYSTICK_H */
