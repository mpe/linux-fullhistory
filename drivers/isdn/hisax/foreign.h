/* $Id: foreign.h,v 1.1 1998/11/09 07:48:57 baccala Exp $
 *
 * HiSax ISDN driver - foreign chipset interface
 *
 * Author       Brent Baccala (baccala@FreeSoft.org)
 *
 *
 *
 * $Log: foreign.h,v $
 * Revision 1.1  1998/11/09 07:48:57  baccala
 * Initial DBRI ISDN code.  Sometimes works (brings up the link and you
 * can telnet through it), sometimes doesn't (crashes the machine)
 *
 */

/*
 *       ISDN operations
 *
 * Many of these routines take an "int dev" argument, which is simply
 * an index into the drivers[] array.  Currently, we only support a
 * single foreign chip, so the value should always be 0.  B channel
 * operations require an "int chan", which should be 0 for channel B1
 * and 1 for channel B2
 *
 * int get_irqnum(int dev)
 *
 *   returns the interrupt number being used by the chip.  ISDN4linux
 *   uses this number to watch the interrupt during initialization and
 *   make sure something is happening.
 *
 * int get_liu_state(int dev)
 *
 *   returns the current state of the ISDN Line Interface Unit (LIU)
 *   as a number between 2 (state F2) and 7 (state F7).  0 may also be
 *   returned if the chip doesn't exist or the LIU hasn't been
 *   activated.  The meanings of the states are defined in I.430, ISDN
 *   BRI Physical Layer Interface.  The most important two states are
 *   F3 (shutdown) and F7 (syncronized).
 *
 * void liu_init(int dev, void (*callback)(void *), void *callback_arg)
 *
 *   initializes the LIU and optionally registers a callback to be
 *   signaled upon a change of LIU state.  The callback will be called
 *   with a single opaque callback_arg.  Once the callback has been
 *   triggered, get_liu_state can be used to determine the LIU
 *   current state.
 *
 * void liu_activate(int dev, int priority)
 *
 *   requests LIU activation at a given D-channel priority.
 *   Successful activatation is achieved upon entering state F7, which
 *   will trigger any callback previously registered with
 *   liu_init.
 *
 * void liu_deactivate(int dev)
 *
 *   deactivates LIU.  Outstanding D and B channel transactions are
 *   terminated rudely and without callback notification.  LIU change
 *   of state callback will be triggered, however.
 *
 * void dxmit(int dev, __u8 *buffer, unsigned int count,
 *            void (*callback)(void *, int), void *callback_arg)
 *
 *   transmits a packet - specified with buffer, count - over the D-channel
 *   interface.  Buffer should begin with the LAPD address field and
 *   end with the information field.  FCS and flag sequences should not
 *   be included, nor is bit-stuffing required - all these functions are
 *   performed by the chip.  The callback function will be called
 *   DURING THE TOP HALF OF AN INTERRUPT HANDLER and will be passed
 *   both the arbitrary callback_arg and an integer error indication:
 *
 *       0 - successful transmission; ready for next packet
 *   non-0 - error value
 *
 *   The callback routine should defer any time-consuming operations
 *   to a bottom-half handler; however, dxmit may be called
 *   from within the callback to request back-to-back transmission of
 *   a second packet (without repeating the priority/collision mechanism)
 *
 *   A comment about the "collision detect" error, which is signalled
 *   whenever the echoed D-channel data didn't match the transmitted
 *   data.  This is part of ISDN's normal multi-drop T-interface
 *   operation, indicating that another device has attempted simultaneous
 *   transmission, but can also result from line noise.  An immediate
 *   requeue via dxmit is suggested, but repeated collision
 *   errors may indicate a more serious problem.
 *
 * void drecv(int dev, __u8 *buffer, unsigned int size,
 *            void (*callback)(void *, int, unsigned int),
 *            void *callback_arg)
 *
 *   register a buffer - buffer, size - into which a D-channel packet
 *   can be received.  The callback function will be called DURING
 *   THE TOP HALF OF AN INTERRUPT HANDLER and will be passed an
 *   arbitrary callback_arg, an integer error indication and the length
 *   of the received packet, which will start with the address field,
 *   end with the information field, and not contain flag or FCS
 *   bytes.  Bit-stuffing will already have been corrected for.
 *   Possible values of second callback argument "error":
 *
 *       0 - successful reception
 *   non-0 - error value
 *
 * int bopen(int dev, int chan, int hdlcmode, u_char xmit_idle_char)
 *
 *   This function should be called before any other operations on a B
 *   channel.  mode is either non-0 to (de)encapsulate using HDLC or 0
 *   for transparent operation. In addition to arranging for interrupt
 *   handling and channel multiplexing, it sets the xmit_idle_char
 *   which is transmitted on the interface when no data buffer is
 *   available.  Suggested values are: 0 for ISDN audio; FF for HDLC
 *   mark idle; 7E for HDLC flag idle.  Returns 0 on a successful
 *   open; -1 on error.
 *
 *   If the chip doesn't support HDLC encapsulation (the Am7930
 *   doesn't), an error will be returned opening L1_MODE_HDLC; the
 *   HiSax driver should retry with L1_MODE_TRANS, then be prepared to
 *   bit-stuff the data before shipping it to the driver.
 *
 * void bclose(int dev, int chan)
 *
 *   Shuts down a B channel when no longer in use.
 *
 * void bxmit(int dev, int chan, __u8 *buffer, unsigned int count,
 *            void (*callback)(void *, int), void *callback_arg)
 *
 *   transmits a data block - specified with buffer, count - over the
 *   B channel interface specified by dev/chan.  In mode L1_MODE_HDLC,
 *   a complete HDLC frames should be relayed with a single bxmit.
 *   The callback function will be called DURING THE TOP HALF OF AN
 *   INTERRUPT HANDLER and will be passed the arbitrary callback_arg
 *   and an integer error indication:
 *
 *       0 - successful transmission; ready for next packet
 *   non-0 - error
 *
 *   The callback routine should defer any time-consuming operations
 *   to a bottom-half handler; however, bxmit may be called
 *   from within the callback to request back-to-back transmission of
 *   another data block
 *
 * void brecv(int dev, int chan, __u8 *buffer, unsigned int size,
 *            void (*callback)(void *, int, unsigned int), void *callback_arg)
 *
 *   receive a raw data block - specified with buffer, size - over the
 *   B channel interface specified by dev/chan.  The callback function
 *   will be called DURING THE TOP HALF OF AN INTERRUPT HANDLER and
 *   will be passed the arbitrary callback_arg, an integer error
 *   indication and the length of the received packet.  In HDLC mode,
 *   the packet will start with the address field, end with the
 *   information field, and will not contain flag or FCS bytes.
 *   Bit-stuffing will already have been corrected for.
 *
 *   Possible values of second callback argument "error":
 *
 *       0 - successful reception
 *   non-0 - error value
 *
 *   The callback routine should defer any time-consuming operations
 *   to a bottom-half handler; however, brecv may be called
 *   from within the callback to register another buffer and ensure
 *   continuous B channel reception without loss of data
 * */

struct foreign_interface {
    int (*get_irqnum)(int dev);
    int (*get_liu_state)(int dev);
    void (*liu_init)(int dev, void (*callback)(void *), void *callback_arg);
    void (*liu_activate)(int dev, int priority);
    void (*liu_deactivate)(int dev);
    void (*dxmit)(int dev, __u8 *buffer, unsigned int count,
                  void (*callback)(void *, int),
                  void *callback_arg);
    void (*drecv)(int dev, __u8 *buffer, unsigned int size,
                  void (*callback)(void *, int, unsigned int),
                  void *callback_arg);
    int (*bopen)(int dev, unsigned int chan,
                 int hdlcmode, u_char xmit_idle_char);
    void (*bclose)(int dev, unsigned int chan);
    void (*bxmit)(int dev, unsigned int chan,
                  __u8 *buffer, unsigned long count,
                  void (*callback)(void *, int),
                  void *callback_arg);
    void (*brecv)(int dev, unsigned int chan,
                  __u8 *buffer, unsigned long size,
                  void (*callback)(void *, int, unsigned int),
                  void *callback_arg);

    struct foreign_interface *next;
};

extern struct foreign_interface amd7930_foreign_interface;
extern struct foreign_interface dbri_foreign_interface;
