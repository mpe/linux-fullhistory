/*
 *	Sound core interface functions
 */
 
extern int register_sound_special(struct file_operations *, int);
extern int register_sound_mixer(struct file_operations *fops);
extern int register_sound_midi(struct file_operations *fops);
extern int register_sound_dsp(struct file_operations *fops);
extern int register_sound_synth(struct file_operations *fops);

extern void unregister_sound_special(int unit);
extern void unregister_sound_mixer(int unit);
extern void unregister_sound_midi(int unit);
extern void unregister_sound_dsp(int unit);
extern void unregister_sound_synth(int unit);
