/*
 *	DMA buffer calls
 */

int DMAbuf_open(int dev, int mode);
int DMAbuf_release(int dev, int mode);
int DMAbuf_getwrbuffer(int dev, char **buf, int *size, int dontblock);
int DMAbuf_get_curr_buffer(int dev, int *buff_no, char **dma_buf, int *buff_ptr, int *buff_size);
int DMAbuf_getrdbuffer(int dev, char **buf, int *len, int dontblock);
int DMAbuf_rmchars(int dev, int buff_no, int c);
int DMAbuf_start_output(int dev, int buff_no, int l);
int DMAbuf_set_count(int dev, int buff_no, int l);
int DMAbuf_ioctl(int dev, unsigned int cmd, caddr_t arg, int local);
void DMAbuf_init(void);
int DMAbuf_start_dma (int dev, unsigned long physaddr, int count, int dma_mode);
int DMAbuf_open_dma (int dev);
void DMAbuf_close_dma (int dev);
void DMAbuf_reset_dma (int dev);
void DMAbuf_inputintr(int dev);
void DMAbuf_outputintr(int dev, int underflow_flag);
int DMAbuf_select(int dev, struct fileinfo *file, int sel_type, select_table_handle * wait);
void DMAbuf_start_devices(unsigned int devmask);

/*
 *	System calls for /dev/dsp and /dev/audio
 */

int audio_read (int dev, struct fileinfo *file, char *buf, int count);
int audio_write (int dev, struct fileinfo *file, const char *buf, int count);
int audio_open (int dev, struct fileinfo *file);
void audio_release (int dev, struct fileinfo *file);
int audio_ioctl (int dev, struct fileinfo *file,
	   unsigned int cmd, caddr_t arg);
int audio_lseek (int dev, struct fileinfo *file, off_t offset, int orig);
void audio_init (void);

int audio_select(int dev, struct fileinfo *file, int sel_type, select_table_handle * wait);

/*
 *	System calls for the /dev/sequencer
 */

int sequencer_read (int dev, struct fileinfo *file, char *buf, int count);
int sequencer_write (int dev, struct fileinfo *file, const char *buf, int count);
int sequencer_open (int dev, struct fileinfo *file);
void sequencer_release (int dev, struct fileinfo *file);
int sequencer_ioctl (int dev, struct fileinfo *file,
	   unsigned int cmd, caddr_t arg);
int sequencer_lseek (int dev, struct fileinfo *file, off_t offset, int orig);
void sequencer_init (void);
void sequencer_timer(unsigned long dummy);
int note_to_freq(int note_num);
unsigned long compute_finetune(unsigned long base_freq, int bend, int range);
void seq_input_event(unsigned char *event, int len);
void seq_copy_to_input (unsigned char *event, int len);

int sequencer_select(int dev, struct fileinfo *file, int sel_type, select_table_handle * wait);

/*
 *	System calls for the /dev/midi
 */

int MIDIbuf_read (int dev, struct fileinfo *file, char *buf, int count);
int MIDIbuf_write (int dev, struct fileinfo *file, const char *buf, int count);
int MIDIbuf_open (int dev, struct fileinfo *file);
void MIDIbuf_release (int dev, struct fileinfo *file);
int MIDIbuf_ioctl (int dev, struct fileinfo *file,
	   unsigned int cmd, caddr_t arg);
int MIDIbuf_lseek (int dev, struct fileinfo *file, off_t offset, int orig);
void MIDIbuf_bytes_received(int dev, unsigned char *buf, int count);
void MIDIbuf_init(void);

int MIDIbuf_select(int dev, struct fileinfo *file, int sel_type, select_table_handle * wait);

/*
 *
 *	Misc calls from various sources
 */

/*	From soundcard.c	*/
void soundcard_init(void);
void tenmicrosec(int *osp);
void request_sound_timer (int count);
void sound_stop_timer(void);
int snd_ioctl_return(int *addr, int value);
int snd_set_irq_handler (int interrupt_level, void(*iproc)(int, void*, struct pt_regs *), char *name, int *osp);
void snd_release_irq(int vect);
void sound_dma_malloc(int dev);
void sound_dma_free(int dev);
void conf_printf(char *name, struct address_info *hw_config);
void conf_printf2(char *name, int base, int irq, int dma, int dma2);

/*	From sound_switch.c	*/
int sound_read_sw (int dev, struct fileinfo *file, char *buf, int count);
int sound_write_sw (int dev, struct fileinfo *file, const char *buf, int count);
int sound_open_sw (int dev, struct fileinfo *file);
void sound_release_sw (int dev, struct fileinfo *file);
int sound_ioctl_sw (int dev, struct fileinfo *file,
	     unsigned int cmd, caddr_t arg);

/*	From opl3.c	*/
int opl3_detect (int ioaddr, int *osp);
void opl3_init(int ioaddr, int *osp);

/*	From sb_card.c	*/
void attach_sb_card(struct address_info *hw_config);
int probe_sb(struct address_info *hw_config);

/*	From sb_common.c */
void sb_dsp_disable_midi(int port);
void sb_dsp_disable_recording(int port);
void attach_sbmpu (struct address_info *hw_config);
int probe_sbmpu (struct address_info *hw_config);
void unload_sbmpu (struct address_info *hw_config);

/*	From uart401.c */
void attach_uart401 (struct address_info *hw_config);
int probe_uart401 (struct address_info *hw_config);
void unload_uart401 (struct address_info *hw_config);
void uart401intr (int irq, void *dev_id, struct pt_regs * dummy);

/*	From adlib_card.c	*/
void attach_adlib_card(struct address_info *hw_config);
int probe_adlib(struct address_info *hw_config);

/*	From pas_card.c	*/
void attach_pas_card(struct address_info *hw_config);
int probe_pas(struct address_info *hw_config);
int pas_set_intr(int mask);
int pas_remove_intr(int mask);
unsigned char pas_read(int ioaddr);
void pas_write(unsigned char data, int ioaddr);

/*	From pas_audio.c */
void pas_pcm_interrupt(unsigned char status, int cause);
void pas_pcm_init(struct address_info *hw_config);

/*	From pas_mixer.c */
int pas_init_mixer(void);

/*	From pas_midi.c */
void pas_midi_init(void);
void pas_midi_interrupt(void);

/*	From gus_card.c */
void attach_gus_card(struct address_info * hw_config);
int probe_gus(struct address_info *hw_config);
int gus_set_midi_irq(int num);
void gusintr(int irq, void *dev_id, struct pt_regs * dummy);
void attach_gus_db16(struct address_info * hw_config);
int probe_gus_db16(struct address_info *hw_config);

/*	From gus_wave.c */
int gus_wave_detect(int baseaddr);
void gus_wave_init(struct address_info *hw_config);
void gus_wave_unload (void);
void gus_voice_irq(void);
unsigned char gus_read8 (int reg);
void gus_write8(int reg, unsigned int data);
void guswave_dma_irq(void);
void gus_delay(void);
int gus_default_mixer_ioctl (int dev, unsigned int cmd, caddr_t arg);
void gus_timer_command (unsigned int addr, unsigned int val);

/*	From gus_midi.c */
void gus_midi_init(void);
void gus_midi_interrupt(int dummy);

/*	From mpu401.c */
void attach_mpu401(struct address_info * hw_config);
int probe_mpu401(struct address_info *hw_config);
void mpuintr(int irq, void *dev_id, struct pt_regs * dummy);

/*	From uart6850.c */
void attach_uart6850(struct address_info * hw_config);
int probe_uart6850(struct address_info *hw_config);

/*	From opl3.c */
void enable_opl3_mode(int left, int right, int both);

/*	From patmgr.c */
int pmgr_open(int dev);
void pmgr_release(int dev);
int pmgr_read (int dev, struct fileinfo *file, char * buf, int count);
int pmgr_write (int dev, struct fileinfo *file, const char * buf, int count);
int pmgr_access(int dev, struct patmgr_info *rec);
int pmgr_inform(int dev, int event, unsigned long parm1, unsigned long parm2,
				    unsigned long parm3, unsigned long parm4);

/* 	From ics2101.c */
void ics2101_mixer_init(void);

/*	From sound_timer.c */
void sound_timer_interrupt(void);
void sound_timer_syncinterval(unsigned int new_usecs);

/*	From ad1848.c */
void ad1848_init (char *name, int io_base, int irq, int dma_playback, int dma_capture, int share_dma, int *osp);
void ad1848_unload (int io_base, int irq, int dma_playback, int dma_capture, int share_dma);

int ad1848_detect (int io_base, int *flags, int *osp);
#define AD_F_CS4231	0x0001	/* Returned if a CS4232 (or compatible) detected */
#define AD_F_CS4248	0x0001	/* Returned if a CS4248 (or compatible) detected */

void     ad1848_interrupt (int irq, void *dev_id, struct pt_regs * dummy);
void attach_ms_sound(struct address_info * hw_config);
int probe_ms_sound(struct address_info *hw_config);
void attach_pnp_ad1848(struct address_info * hw_config);
int probe_pnp_ad1848(struct address_info *hw_config);
void unload_pnp_ad1848(struct address_info *hw_info);

/* 	From pss.c */
int probe_pss (struct address_info *hw_config);
void attach_pss (struct address_info *hw_config);
int probe_pss_mpu (struct address_info *hw_config);
void attach_pss_mpu (struct address_info *hw_config);
int probe_pss_mss (struct address_info *hw_config);
void attach_pss_mss (struct address_info *hw_config);

/* 	From sscape.c */
int probe_sscape (struct address_info *hw_config);
void attach_sscape (struct address_info *hw_config);
int probe_ss_ms_sound (struct address_info *hw_config);
void attach_ss_ms_sound(struct address_info * hw_config);

int pss_read (int dev, struct fileinfo *file, char *buf, int count);
int pss_write (int dev, struct fileinfo *file, char *buf, int count);
int pss_open (int dev, struct fileinfo *file);
void pss_release (int dev, struct fileinfo *file);
int pss_ioctl (int dev, struct fileinfo *file,
	   unsigned int cmd, caddr_t arg);
int pss_lseek (int dev, struct fileinfo *file, off_t offset, int orig);
void pss_init(void);

/* From aedsp16.c */
int InitAEDSP16_SBPRO(struct address_info *hw_config);
int InitAEDSP16_MSS(struct address_info *hw_config);
int InitAEDSP16_MPU401(struct address_info *hw_config);

/*	From midi_synth.c	*/
void do_midi_msg (int synthno, unsigned char *msg, int mlen);

/*	From trix.c	*/
void attach_trix_wss (struct address_info *hw_config);
int probe_trix_wss (struct address_info *hw_config);
void attach_trix_sb (struct address_info *hw_config);
int probe_trix_sb (struct address_info *hw_config);
void attach_trix_mpu (struct address_info *hw_config);
int probe_trix_mpu (struct address_info *hw_config);

/*	From mad16.c	*/
void attach_mad16 (struct address_info *hw_config);
int probe_mad16 (struct address_info *hw_config);
void attach_mad16_mpu (struct address_info *hw_config);
int probe_mad16_mpu (struct address_info *hw_config);
int mad16_sb_dsp_detect (struct address_info *hw_config);

/*	Unload routines from various source files*/
void unload_pss(struct address_info *hw_info);
void unload_pss_mpu(struct address_info *hw_info);
void unload_pss_mss(struct address_info *hw_info);
void unload_mad16(struct address_info *hw_info);
void unload_mad16_mpu(struct address_info *hw_info);
void unload_adlib(struct address_info *hw_info);
void unload_pas(struct address_info *hw_info);
void unload_mpu401(struct address_info *hw_info);
void unload_maui(struct address_info *hw_info);
void unload_uart6850(struct address_info *hw_info);
void unload_sb(struct address_info *hw_info);
void unload_sb16(struct address_info *hw_info);
void unload_sb16midi(struct address_info *hw_info);
void unload_gus_db16(struct address_info *hw_info);
void unload_ms_sound(struct address_info *hw_info);
void unload_gus(struct address_info *hw_info);
void unload_sscape(struct address_info *hw_info);
void unload_ss_ms_sound(struct address_info *hw_info);
void unload_trix_wss(struct address_info *hw_info);
void unload_trix_sb(struct address_info *hw_info);
void unload_trix_mpu(struct address_info *hw_info);
void unload_cs4232(struct address_info *hw_info);
void unload_cs4232_mpu(struct address_info *hw_info);

/* From cs4232.c */

int probe_cs4232 (struct address_info *hw_config);
void attach_cs4232 (struct address_info *hw_config);
int probe_cs4232_mpu (struct address_info *hw_config);
void attach_cs4232_mpu (struct address_info *hw_config);

/*	From maui.c */
void attach_maui(struct address_info * hw_config);
int probe_maui(struct address_info *hw_config);

/*	From sound_pnp.c */
void sound_pnp_init(int *osp);
void sound_pnp_disconnect(void);
