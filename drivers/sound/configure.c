/*
 *     PnP soundcard support is not included in this version.
 *
 *       AEDSP16 will not work without significant changes.
 */
#define DISABLED_OPTIONS 	(B(OPT_SPNP)|B(OPT_AEDSP16)|B(OPT_UNUSED1)|B(OPT_UNUSED2))
/*
 * sound/configure.c  - Configuration program for the Linux Sound Driver
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#define B(x)	(1 << (x))

/*
 * Option numbers
 */

#define OPT_PAS		0
#define OPT_SB		1
#define OPT_ADLIB	2
#define OPT_LAST_MUTUAL	2

#define OPT_GUS		3
#define OPT_MPU401	4
#define OPT_UART6850	5
#define OPT_PSS		6
#define OPT_GUS16	7
#define OPT_GUSMAX	8
#define OPT_MSS		9
#define OPT_SSCAPE	10
#define OPT_TRIX	11
#define OPT_MAD16	12
#define OPT_CS4232	13
#define OPT_MAUI	14
#define OPT_SPNP		15

#define OPT_HIGHLEVEL   16	/* This must be same than the next one */
#define OPT_UNUSED1	16
#define OPT_UNUSED2	17
#define OPT_AEDSP16     18
#define OPT_AUDIO	19
#define OPT_MIDI_AUTO	20
#define OPT_MIDI	21
#define OPT_YM3812_AUTO	22
#define OPT_YM3812	23
#define OPT_LAST	23	/* Last defined OPT number */

#define DUMMY_OPTS (B(OPT_MIDI_AUTO)|B(OPT_YM3812_AUTO))

#define ANY_DEVS (B(OPT_AUDIO)|B(OPT_MIDI)|B(OPT_GUS)| \
		  B(OPT_MPU401)|B(OPT_PSS)|B(OPT_GUS16)|B(OPT_GUSMAX)| \
		  B(OPT_MSS)|B(OPT_SSCAPE)|B(OPT_UART6850)|B(OPT_TRIX)| \
		  B(OPT_MAD16)|B(OPT_CS4232)|B(OPT_MAUI)|B(OPT_ADLIB))
#define AUDIO_CARDS (B (OPT_PSS) | B (OPT_SB) | B (OPT_PAS) | B (OPT_GUS) | \
		B (OPT_MSS) | B (OPT_GUS16) | B (OPT_GUSMAX) | B (OPT_TRIX) | \
		B (OPT_SSCAPE)| B(OPT_MAD16) | B(OPT_CS4232))
#define MPU_DEVS (B(OPT_PSS)|\
		  B(OPT_CS4232)|B(OPT_SPNP)|B(OPT_MAUI))
#define UART401_DEVS (SBDSP_DEVS|B(OPT_TRIX)|B(OPT_MAD16)|B(OPT_SSCAPE))
#define MIDI_CARDS (MPU_DEVS | UART401_DEVS | \
		    B (OPT_PSS) | B (OPT_SB) | B (OPT_PAS) | B (OPT_MPU401) | \
		    B (OPT_GUS) | B (OPT_TRIX) | B (OPT_SSCAPE)|B(OPT_MAD16) | \
		    B (OPT_CS4232)|B(OPT_MAUI))
#define AD1848_DEVS (B(OPT_GUS16)|B(OPT_MSS)|B(OPT_PSS)|B(OPT_GUSMAX)|\
		     B(OPT_SSCAPE)|B(OPT_TRIX)|B(OPT_MAD16)|B(OPT_CS4232)|\
		     B(OPT_SPNP))
#define SBDSP_DEVS (B(OPT_SB)|B(OPT_SPNP)|B(OPT_MAD16))
#define SEQUENCER_DEVS (OPT_MIDI|OPT_YM3812|OPT_ADLIB|OPT_GUS|OPT_MAUI|MIDI_CARDS)
/*
 * Options that have been disabled for some reason (incompletely implemented
 * and/or tested). Don't remove from this list before looking at file
 * experimental.txt for further info.
 */

typedef struct
  {
    unsigned long   conditions;
    unsigned long   exclusive_options;
    char            macro[20];
    int             verify;
    int             alias;
    int             default_answ;
  }

hw_entry;


/*
 * The rule table for the driver options. The first field defines a set of
 * options which must be selected before this entry can be selected. The
 * second field is a set of options which are not allowed with this one. If
 * the fourth field is zero, the option is selected without asking
 * confirmation from the user.
 *
 * With this version of the rule table it is possible to select just one type of
 * hardware.
 *
 * NOTE!        Keep the following table and the questions array in sync with the
 * option numbering!
 */

hw_entry        hw_table[] =
{
/*
 * 0
 */
  {0, 0, "PAS", 1, 0, 0},
  {0, 0, "SB", 1, 0, 0},
  {0, B (OPT_PAS) | B (OPT_SB), "ADLIB", 1, 0, 0},

  {0, 0, "GUS", 1, 0, 0},
  {0, 0, "MPU401", 1, 0, 0},
  {0, 0, "UART6850", 1, 0, 0},
  {0, 0, "PSS", 1, 0, 0},
  {B (OPT_GUS), 0, "GUS16", 1, 0, 0},
  {B (OPT_GUS), B (OPT_GUS16), "GUSMAX", 1, 0, 0},
  {0, 0, "MSS", 1, 0, 0},
  {0, 0, "SSCAPE", 1, 0, 0},
  {0, 0, "TRIX", 1, 0, 0},
  {0, 0, "MAD16", 1, 0, 0},
  {0, 0, "CS4232", 1, 0, 0},
  {0, 0, "MAUI", 1, 0, 0},
  {0, 0, "SPNP", 1, 0, 0},

  {B (OPT_SB), B (OPT_PAS), "UNUSED1", 1, 0, 1},
  {B (OPT_SB) | B (OPT_UNUSED1), B (OPT_PAS), "UNUSED2", 1, 0, 1},
  {B (OPT_UNUSED1) | B (OPT_MSS) | B (OPT_MPU401), 0, "AEDSP16", 1, 0, 0},
  {AUDIO_CARDS, 0, "AUDIO", 1, 0, 1},
  {B (OPT_MPU401) | B (OPT_MAUI), 0, "MIDI_AUTO", 0, OPT_MIDI, 0},
  {MIDI_CARDS, 0, "MIDI", 1, 0, 1},
  {B (OPT_ADLIB), 0, "YM3812_AUTO", 0, OPT_YM3812, 0},
  {B (OPT_PSS) | B (OPT_SB) | B (OPT_PAS) | B (OPT_ADLIB) | B (OPT_MSS) | B (OPT_PSS), B (OPT_YM3812_AUTO), "YM3812", 1, 0, 1}
};

char           *questions[] =
{
  "ProAudioSpectrum 16 support",
  "SoundBlaster (SB, SBPro, SB16, clones) support",
  "Generic OPL2/OPL3 FM synthesizer support",
  "Gravis Ultrasound support",
  "MPU-401 support (NOT for SB16)",
  "6850 UART Midi support",
  "PSS (ECHO-ADI2111) support",
  "16 bit sampling option of GUS (_NOT_ GUS MAX)",
  "GUS MAX support",
  "Microsoft Sound System support",
  "Ensoniq Soundscape support",
  "MediaTrix AudioTrix Pro support",
  "Support for MAD16 and/or Mozart based cards",
  "Support for Crystal CS4232 based (PnP) cards",
  "Support for Turtle Beach Wave Front (Maui, Tropez) synthesizers",
  "Support for PnP sound cards (_EXPERIMENTAL_)",

  "*** Unused option 1 ***",
  "*** Unused option 2 ***",
  "Audio Excel DSP 16 initialization support",
  "/dev/dsp and /dev/audio support",
  "This should not be asked",
  "MIDI interface support",
  "This should not be asked",
  "FM synthesizer (YM3812/OPL-3) support",
  "Is the sky really falling"
};

/* help text for each option */
char           *help[] =
{
  "Enable this option only if you have a Pro Audio Spectrum 16,\n"
  "Pro Audio Studio 16, or Logitech SoundMan 16. Don't enable this if\n"
  "you have some other card made by MediaVision or Logitech as\n"
  "they are not PAS16 compatible.\n",

  "Enable this if you have an original SoundBlaster card made by\n"
  "Creative Labs or a 100%% hardware compatible clone. For an\n"
  "unknown card you may want to try this if it claims to be\n"
  "SoundBlaster compatible.\n",

  "Enable this option if your sound card has a Yamaha OPL2 or OPL3\n"
  "FM synthesizer chip.\n",

  "Enable this option for any type of Gravis Ultrasound card\n"
  "including the GUS or GUS MAX.\n",

  "The MPU401 interface is supported by almost all sound cards. However,\n"
  "some natively supported cards have their own driver for\n"
  "MPU401. Enabling the MPU401 option with these cards will cause a\n"
  "conflict. Also enabling MPU401 on a system that doesn't really have a\n"
  "MPU401 could cause some trouble. It's safe to enable this if you have a\n"
  "true MPU401 MIDI interface card.\n",

  "This option enables support for MIDI interfaces based on the 6850\n"
  "UART chip. This interface is rarely found on sound cards.\n",

  "Enable this option if you have an Orchid SW32, Cardinal DSP16 or other\n"
  "sound card based on the PSS chipset (AD1848 codec, ADSP-2115 DSP chip,\n"
  "and Echo ESC614 ASIC CHIP).\n",

  "Enable this if you have installed the 16-bit sampling daughtercard on\n"
  "your GUS card. Do not use if you have a GUS MAX as enabling this option\n"
  "disables GUS MAX support.\n",

  "Enable this option if you have a Gravis Ultrasound MAX sound\n"
  "card\n",

  "Enable this option if you have the original Windows Sound System\n"
  "card made by Microsoft or the Aztech SG 16 Pro or NX16 Pro.\n",

  "Enable this if you have a sound card based on the Ensoniq\n"
  "Soundscape chipset. Such cards are being manufactured by Ensoniq,\n"
  "Spea and Reveal (Reveal makes other cards as well).\n",

  "Enable this option if you have the AudioTrix Pro sound card\n"
  "manufactured by MediaTrix.\n",

  "Enable this if your card has a Mozart (OAK OTI-601) or MAD16 (OPTi\n"
  "82C928 or 82C929) audio interface chip. These chips are currently\n"
  "quite common so it's possible that many no-name cards have one of\n"
  "them. In addition the MAD16 chip is used in some cards made by known\n"
  "manufacturers such as Turtle Beach (Tropez), Reveal (some models) and\n"
  "Diamond (latest ones).\n",

  "Enable this if you have a card based on the Crystal CS4232 chip set.\n",

  "Enable this option if you have a Turtle Beach Wave Front, Maui,\n"
  "or Tropez sound card.\n",

  "Use this option to enable experimental support for cards that\n"
  "use the Plug and Play protocol.\n",

  "Enable this option if your card is a SoundBlaster Pro or\n"
  "SoundBlaster 16. It also works with many SoundBlaster Pro clones.\n",

  "Enable this if you have a SoundBlaster 16, including the AWE32.\n",

  "Enable this if you have an Audio Excel DSP16 card. See the file\n"
  "Readme.aedsp16 for more information.\n",

  "This option enables the A/D and D/A converter (PCM) devices\n"
  "supported by almost all sound cards.\n",

  "This should not be asked",

  "This enables the dev/midixx devices and access to any MIDI ports\n"
  "using /dev/sequencer and /dev/music. This option also affects any\n"
  "MPU401 and/or General MIDI compatible devices.\n",

  "This should not be asked",

  "This enables the Yamaha FM synthesizer chip used on many sound\n"
  "cards.\n",

  "Is the sky really falling"
};

struct kludge
  {
    char           *name;
    int             mask;
  }
extra_options[] =
{
  {
    "MPU_EMU", MPU_DEVS
  }
  ,
  {
    "AD1848", AD1848_DEVS
  }
  ,
  {
    "SBDSP", SBDSP_DEVS
  }
  ,
  {
    "UART401", UART401_DEVS
  }
  ,
  {
    "SEQUENCER", SEQUENCER_DEVS
  }
  ,
  {
    NULL, 0
  }
};

char           *oldconf = "/etc/soundconf";

int             old_config_used = 0;
int             def_size, sb_base = 0;

unsigned long   selected_options = 0;
int             sb_dma = 0;

int             dump_only = 0;

void            build_defines (void);

#include "hex2hex.h"
int             bin2hex (char *path, char *target, char *varname);

int
can_select_option (int nr)
{

  if (hw_table[nr].conditions)
    if (!(hw_table[nr].conditions & selected_options))
      return 0;

  if (hw_table[nr].exclusive_options)
    if (hw_table[nr].exclusive_options & selected_options)
      return 0;

  if (DISABLED_OPTIONS & B (nr))
    return 0;

  return 1;
}

int
think_positively (char *prompt, int def_answ, char *help)
{
  char            answ[512];
  int             len;

response:
  fprintf (stderr, prompt);
  if (def_answ)
    fprintf (stderr, " [Y/n/?] ");
  else
    fprintf (stderr, " [N/y/?] ");

  if ((len = read (0, answ, sizeof (answ))) < 1)
    {
      fprintf (stderr, "\n\nERROR! Cannot read stdin\n");

      perror ("stdin");
      printf ("invalid_configuration__run_make_config_again\n");
      exit (-1);
    }

  if (len < 2)			/*
				 * There is an additional LF at the end
				 */
    return def_answ;

  if (answ[0] == '?')
    {				/* display help message */
      fprintf (stderr, "\n");
      fprintf (stderr, help);
      fprintf (stderr, "\n");
      goto response;
    }

  answ[len - 1] = 0;

  if (!strcmp (answ, "y") || !strcmp (answ, "Y"))
    return 1;

  return 0;
}

int
ask_value (char *format, int default_answer)
{
  char            answ[512];
  int             len, num;

play_it_again_Sam:

  if ((len = read (0, answ, sizeof (answ))) < 1)
    {
      fprintf (stderr, "\n\nERROR! Cannot read stdin\n");

      perror ("stdin");
      printf ("invalid_configuration__run_make_config_again\n");
      exit (-1);
    }

  if (len < 2)			/*
				 * There is an additional LF at the end
				 */
    return default_answer;

  answ[len - 1] = 0;

  if (sscanf (answ, format, &num) != 1)
    {
      fprintf (stderr, "Illegal format. Try again: ");
      goto play_it_again_Sam;
    }

  return num;
}

#define FMT_HEX 1
#define FMT_INT 2

void
ask_int_choice (int mask, char *macro,
		char *question,
		int format,
		int defa,
		char *choices)
{
  int             num, i;

  if (dump_only)
    {

      for (i = 0; i < OPT_LAST; i++)
	if (mask == B (i))
	  {
	    unsigned int    j;

	    for (j = 0; j < strlen (choices); j++)
	      if (choices[j] == '\'')
		choices[j] = '_';

	    printf ("\nif [ \"$CONFIG_%s\" = \"y\" ]; then\n",
		    hw_table[i].macro);
	    if (format == FMT_INT)
	      printf ("int '%s %s' %s %d\n", question, choices, macro, defa);
	    else
	      printf ("hex '%s %s' %s %x\n", question, choices, macro, defa);
	    printf ("fi\n");
	  }
    }
  else
    {
      if (!(mask & selected_options))
	return;

      fprintf (stderr, "\n%s\n", question);
      if (strcmp (choices, ""))
	fprintf (stderr, "Possible values are: %s\n", choices);

      if (format == FMT_INT)
	{
	  if (defa == -1)
	    fprintf (stderr, "\t(-1 disables this feature)\n");
	  fprintf (stderr, "The default value is %d\n", defa);
	  fprintf (stderr, "Enter the value: ");
	  num = ask_value ("%d", defa);
	  if (num == -1)
	    return;
	  fprintf (stderr, "%s set to %d.\n", question, num);
	  printf ("#define %s %d\n", macro, num);
	}
      else
	{
	  if (defa == 0)
	    fprintf (stderr, "\t(0 disables this feature)\n");
	  fprintf (stderr, "The default value is %x\n", defa);
	  fprintf (stderr, "Enter the value: ");
	  num = ask_value ("%x", defa);
	  if (num == 0)
	    return;
	  fprintf (stderr, "%s set to %x.\n", question, num);
	  printf ("#define %s 0x%x\n", macro, num);
	}
    }
}

void
rebuild_file (char *line)
{
  char           *method, *next, *old, *var, *p;

  method = p = line;

  while (*p && *p != ' ')
    p++;
  *p++ = 0;

  old = p;
  while (*p && *p != ' ')
    p++;
  *p++ = 0;

  next = p;
  while (*p && *p != ' ')
    p++;
  *p++ = 0;

  var = p;
  while (*p && *p != ' ')
    p++;
  *p++ = 0;

  fprintf (stderr, "Rebuilding file `%s' (%s %s)\n", next, method, old);

  if (strcmp (method, "bin2hex") == 0)
    {
      if (!bin2hex (old, next, var))
	{
	  fprintf (stderr, "Rebuild failed\n");
	  exit (-1);
	}
    }
  else if (strcmp (method, "hex2hex") == 0)
    {
      if (!hex2hex (old, next, var))
	{
	  fprintf (stderr, "Rebuild failed\n");
	  exit (-1);
	}
    }
  else
    {
      fprintf (stderr, "Failed to build `%s' - unknown method %s\n",
	       next, method);
      exit (-1);
    }
}

int
use_old_config (char *filename)
{
  char            buf[1024];
  int             i = 0;

  FILE           *oldf;

  fprintf (stderr, "Copying old configuration from `%s'\n", filename);

  if ((oldf = fopen (filename, "r")) == NULL)
    {
      fprintf (stderr, "Couldn't open previous configuration file\n");
      perror (filename);
      return 0;
    }

  while (fgets (buf, 1024, oldf) != NULL)
    {
      char            tmp[100];

      if (buf[0] != '#')
	{
	  printf ("%s", buf);

	  strncpy (tmp, buf, 8);
	  tmp[8] = 0;

	  if (strcmp (tmp, "/*build ") == 0)
	    rebuild_file (&buf[8]);

	  continue;
	}

      strncpy (tmp, buf, 8);
      tmp[8] = 0;

      if (strcmp (tmp, "#define ") == 0)
	{
	  char           *id = &buf[8];

	  i = 0;
	  while (id[i] && id[i] != ' ' &&
		 id[i] != '\t' && id[i] != '\n')
	    i++;

	  strncpy (tmp, id, i);
	  tmp[i] = 0;

	  if (strcmp (tmp, "SELECTED_SOUND_OPTIONS") == 0)
	    continue;

	  if (strcmp (tmp, "KERNEL_SOUNDCARD") == 0)
	    continue;

	  if (strcmp (tmp, "JAZZ_DMA16") == 0)	/* Rename it (hack) */
	    {
	      printf ("#define SB_DMA2 %s\n",
		      &buf[18]);
	      continue;
	    }

	  if (strcmp (tmp, "SB16_DMA") == 0)	/* Rename it (hack) */
	    {
	      printf ("#define SB_DMA2 %s\n",
		      &buf[16]);
	      continue;
	    }

	  tmp[8] = 0;		/* Truncate the string */
	  if (strcmp (tmp, "EXCLUDE_") == 0)
	    continue;		/* Skip excludes */

	  strncpy (tmp, id, i);
	  tmp[7] = 0;		/* Truncate the string */

	  if (strcmp (tmp, "CONFIG_") == 0)
	    {
	      strncpy (tmp, &id[7], i - 7);
	      tmp[i - 7] = 0;

	      for (i = 0; i <= OPT_LAST; i++)
		if (strcmp (hw_table[i].macro, tmp) == 0)
		  {
		    selected_options |= (1 << i);
		    break;
		  }
	      continue;
	    }

	  printf ("%s", buf);
	  continue;
	}

      if (strcmp (tmp, "#undef  ") == 0)
	{
	  char           *id = &buf[8];

	  i = 0;
	  while (id[i] && id[i] != ' ' &&
		 id[i] != '\t' && id[i] != '\n')
	    i++;

	  strncpy (tmp, id, i);
	  tmp[7] = 0;		/* Truncate the string */
	  if (strcmp (tmp, "CONFIG_") == 0)
	    continue;

	  strncpy (tmp, id, i);

	  tmp[8] = 0;		/* Truncate the string */
	  if (strcmp (tmp, "EXCLUDE_") != 0)
	    continue;		/* Not a #undef  EXCLUDE_ line */
	  strncpy (tmp, &id[8], i - 8);
	  tmp[i - 8] = 0;

	  for (i = 0; i <= OPT_LAST; i++)
	    if (strcmp (hw_table[i].macro, tmp) == 0)
	      {
		selected_options |= (1 << i);
		break;
	      }
	  continue;
	}

      printf ("%s", buf);
    }
  fclose (oldf);

  for (i = 0; i <= OPT_LAST; i++)
    if (!hw_table[i].alias)
      if (selected_options & B (i))
	printf ("#define CONFIG_%s\n", hw_table[i].macro);
      else
	printf ("#undef  CONFIG_%s\n", hw_table[i].macro);


  printf ("\n");

  i = 0;

  while (extra_options[i].name != NULL)
    {
      if (selected_options & extra_options[i].mask)
	printf ("#define CONFIG_%s\n", extra_options[i].name);
      else
	printf ("#undef  CONFIG_%s\n", extra_options[i].name);
      i++;
    }

  printf ("\n");

  printf ("#define SELECTED_SOUND_OPTIONS\t0x%08x\n", selected_options);
  fprintf (stderr, "Old configuration copied.\n");

#if defined(linux) || defined(Solaris)
  build_defines ();
#endif
  old_config_used = 1;
  return 1;
}

#if defined(linux) || defined(Solaris)
void
build_defines (void)
{
  FILE           *optf;
  int             i;

  if ((optf = fopen (".defines", "w")) == NULL)
    {
      perror (".defines");
      exit (-1);
    }


  for (i = 0; i <= OPT_LAST; i++)
    if (!hw_table[i].alias)
      if (selected_options & B (i))
	fprintf (optf, "CONFIG_%s=y\n", hw_table[i].macro);


  fprintf (optf, "\n");

  i = 0;

  while (extra_options[i].name != NULL)
    {
      if (selected_options & extra_options[i].mask)
	fprintf (optf, "CONFIG_%s=y\n", extra_options[i].name);
      i++;
    }

  fprintf (optf, "\n");
  fclose (optf);
}
#endif

void
ask_parameters (void)
{
  int             num;

  build_defines ();
  /*
   * IRQ and DMA settings
   */

  ask_int_choice (B (OPT_AEDSP16), "AEDSP16_BASE",
		  "I/O base for Audio Excel DSP 16",
		  FMT_HEX,
		  0x220,
		  "220 or 240");

  ask_int_choice (B (OPT_SB), "SBC_BASE",
		  "I/O base for SB",
		  FMT_HEX,
		  0x220,
		  "Check from manual of the card");

  ask_int_choice (B (OPT_SB), "SBC_IRQ",
		  "SoundBlaster IRQ",
		  FMT_INT,
		  7,
		  "Check from manual of the card");

  ask_int_choice (B (OPT_SB), "SBC_DMA",
		  "SoundBlaster DMA",
		  FMT_INT,
		  1,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_SB), "SB_DMA2",
		"SoundBlaster 16 bit DMA (_REQUIRED_for SB16, Jazz16, SMW)",
		  FMT_INT,
		  5,
		  "5, 6 or 7 (use 1 for 8 bit cards)");

  ask_int_choice (B (OPT_SB), "SB_MPU_BASE",
		  "MPU401 I/O base of SB16, Jazz16 and ES1688",
		  FMT_HEX,
		  0,
		  "Check from manual of the card");

  ask_int_choice (B (OPT_SB), "SB_MPU_IRQ",
		  "SB MPU401 IRQ (Jazz16, SM Wave and ES1688)",
		  FMT_INT,
		  -1,
		  "Check from manual of the card");

  ask_int_choice (B (OPT_PAS), "PAS_IRQ",
		  "PAS16 IRQ",
		  FMT_INT,
		  10,
		  "3, 4, 5, 7, 9, 10, 11, 12, 14 or 15");

  ask_int_choice (B (OPT_PAS), "PAS_DMA",
		  "PAS16 DMA",
		  FMT_INT,
		  3,
		  "0, 1, 3, 5, 6 or 7");

  if (selected_options & B (OPT_PAS))
    {
      if (think_positively ("Enable Joystick port on ProAudioSpectrum", 0,
	"Enable this option if you want to use the joystick port provided\n"
			    "on the PAS sound card.\n"))
	printf ("#define PAS_JOYSTICK_ENABLE\n");

      if (think_positively ("Enable PAS16 bus clock option", 0,
       "The PAS16 can be noisy with some motherboards. There is a command\n"
	"line switch (:T?) in the DOS driver for PAS16 which solves this.\n"
      "Don't enable this feature unless you have problems and have to use\n"
			    "this switch with DOS\n"))
	printf ("#define BROKEN_BUS_CLOCK\n");

      if (think_positively ("Disable SB mode of PAS16", 0,
	     "You should disable SB emulation of PAS16 if you want to use\n"
			 "Another SB compatible card in the same system\n"))
	printf ("#define DISABLE_SB_EMULATION\n");
    }

  ask_int_choice (B (OPT_GUS), "GUS_BASE",
		  "I/O base for GUS",
		  FMT_HEX,
		  0x220,
		  "210, 220, 230, 240, 250 or 260");


  ask_int_choice (B (OPT_GUS), "GUS_IRQ",
		  "GUS IRQ",
		  FMT_INT,
		  15,
		  "3, 5, 7, 9, 11, 12 or 15");

  ask_int_choice (B (OPT_GUS), "GUS_DMA",
		  "GUS DMA",
		  FMT_INT,
		  6,
		  "1, 3, 5, 6 or 7");

  ask_int_choice (B (OPT_GUS), "GUS_DMA2",
		  "Second DMA channel for GUS",
		  FMT_INT,
		  -1,
		  "1, 3, 5, 6 or 7");

  ask_int_choice (B (OPT_GUS16), "GUS16_BASE",
		  "I/O base for the 16 bit daughtercard of GUS",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");


  ask_int_choice (B (OPT_GUS16), "GUS16_IRQ",
		  "GUS 16 bit daughtercard IRQ",
		  FMT_INT,
		  7,
		  "3, 4, 5, 7, or 9");

  ask_int_choice (B (OPT_GUS16), "GUS16_DMA",
		  "GUS DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_MPU401), "MPU_BASE",
		  "I/O base for MPU401",
		  FMT_HEX,
		  0x330,
		  "Check from manual of the card");

  ask_int_choice (B (OPT_MPU401), "MPU_IRQ",
		  "MPU401 IRQ",
		  FMT_INT,
		  9,
		  "Check from manual of the card");

  ask_int_choice (B (OPT_MAUI), "MAUI_BASE",
		  "I/O base for Maui",
		  FMT_HEX,
		  0x330,
		  "210, 230, 260, 290, 300, 320, 338 or 330");

  ask_int_choice (B (OPT_MAUI), "MAUI_IRQ",
		  "Maui IRQ",
		  FMT_INT,
		  9,
		  "5, 9, 12 or 15");

  ask_int_choice (B (OPT_UART6850), "U6850_BASE",
		  "I/O base for UART 6850 MIDI port",
		  FMT_HEX,
		  0,
		  "(Unknown)");

  ask_int_choice (B (OPT_UART6850), "U6850_IRQ",
		  "UART6850 IRQ",
		  FMT_INT,
		  -1,
		  "(Unknown)");

  ask_int_choice (B (OPT_PSS), "PSS_BASE",
		  "PSS I/O base",
		  FMT_HEX,
		  0x220,
		  "220 or 240");

  ask_int_choice (B (OPT_PSS), "PSS_MSS_BASE",
		  "PSS audio I/O base",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");

  ask_int_choice (B (OPT_PSS), "PSS_MSS_IRQ",
		  "PSS audio IRQ",
		  FMT_INT,
		  11,
		  "7, 9, 10 or 11");

  ask_int_choice (B (OPT_PSS), "PSS_MSS_DMA",
		  "PSS audio DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_PSS), "PSS_MPU_BASE",
		  "PSS MIDI I/O base",
		  FMT_HEX,
		  0x330,
		  "");

  ask_int_choice (B (OPT_PSS), "PSS_MPU_IRQ",
		  "PSS MIDI IRQ",
		  FMT_INT,
		  9,
		  "3, 4, 5, 7 or 9");

  ask_int_choice (B (OPT_MSS), "MSS_BASE",
		  "MSS/WSS I/O base",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");

  ask_int_choice (B (OPT_MSS), "MSS_IRQ",
		  "MSS/WSS IRQ",
		  FMT_INT,
		  11,
		  "7, 9, 10 or 11");

  ask_int_choice (B (OPT_MSS), "MSS_DMA",
		  "MSS/WSS DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_SSCAPE), "SSCAPE_BASE",
		  "Soundscape MIDI I/O base",
		  FMT_HEX,
		  0x330,
		  "320, 330, 340 or 350");

  ask_int_choice (B (OPT_SSCAPE), "SSCAPE_IRQ",
		  "Soundscape MIDI IRQ",
		  FMT_INT,
		  9,
		  "");

  ask_int_choice (B (OPT_SSCAPE), "SSCAPE_DMA",
		  "Soundscape initialization DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_SSCAPE), "SSCAPE_MSS_BASE",
		  "Soundscape audio I/O base",
		  FMT_HEX,
		  0x534,
		  "534, 608, E84 or F44");

  ask_int_choice (B (OPT_SSCAPE), "SSCAPE_MSS_IRQ",
		  "Soundscape audio IRQ",
		  FMT_INT,
		  11,
		  "7, 9, 10 or 11");


  if (selected_options & B (OPT_SSCAPE))
    {
      int             reveal_spea;

      reveal_spea = think_positively (
		  "Is your SoundScape card made/marketed by Reveal or Spea",
				       0,
		 "Enable if you have a SoundScape card with the Reveal or\n"
				       "Spea name on it.\n");
      if (reveal_spea)
	printf ("#define REVEAL_SPEA\n");

    }

  ask_int_choice (B (OPT_TRIX), "TRIX_BASE",
		  "AudioTrix audio I/O base",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");

  ask_int_choice (B (OPT_TRIX), "TRIX_IRQ",
		  "AudioTrix audio IRQ",
		  FMT_INT,
		  11,
		  "7, 9, 10 or 11");

  ask_int_choice (B (OPT_TRIX), "TRIX_DMA",
		  "AudioTrix audio DMA",
		  FMT_INT,
		  0,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_TRIX), "TRIX_DMA2",
		  "AudioTrix second (duplex) DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_TRIX), "TRIX_MPU_BASE",
		  "AudioTrix MIDI I/O base",
		  FMT_HEX,
		  0x330,
		  "330, 370, 3B0 or 3F0");

  ask_int_choice (B (OPT_TRIX), "TRIX_MPU_IRQ",
		  "AudioTrix MIDI IRQ",
		  FMT_INT,
		  9,
		  "3, 4, 5, 7 or 9");

  ask_int_choice (B (OPT_TRIX), "TRIX_SB_BASE",
		  "AudioTrix SB I/O base",
		  FMT_HEX,
		  0x220,
		  "220, 210, 230, 240, 250, 260 or 270");

  ask_int_choice (B (OPT_TRIX), "TRIX_SB_IRQ",
		  "AudioTrix SB IRQ",
		  FMT_INT,
		  7,
		  "3, 4, 5 or 7");

  ask_int_choice (B (OPT_TRIX), "TRIX_SB_DMA",
		  "AudioTrix SB DMA",
		  FMT_INT,
		  1,
		  "1 or 3");

  ask_int_choice (B (OPT_CS4232), "CS4232_BASE",
		  "CS4232 audio I/O base",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");

  ask_int_choice (B (OPT_CS4232), "CS4232_IRQ",
		  "CS4232 audio IRQ",
		  FMT_INT,
		  11,
		  "5, 7, 9, 11, 12 or 15");

  ask_int_choice (B (OPT_CS4232), "CS4232_DMA",
		  "CS4232 audio DMA",
		  FMT_INT,
		  0,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_CS4232), "CS4232_DMA2",
		  "CS4232 second (duplex) DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_CS4232), "CS4232_MPU_BASE",
		  "CS4232 MIDI I/O base",
		  FMT_HEX,
		  0x330,
		  "330, 370, 3B0 or 3F0");

  ask_int_choice (B (OPT_CS4232), "CS4232_MPU_IRQ",
		  "CS4232 MIDI IRQ",
		  FMT_INT,
		  9,
		  "5, 7, 9, 11, 12 or 15");

  ask_int_choice (B (OPT_MAD16), "MAD16_BASE",
		  "MAD16 audio I/O base",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");

  ask_int_choice (B (OPT_MAD16), "MAD16_IRQ",
		  "MAD16 audio IRQ",
		  FMT_INT,
		  11,
		  "7, 9, 10 or 11");

  ask_int_choice (B (OPT_MAD16), "MAD16_DMA",
		  "MAD16 audio DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_MAD16), "MAD16_DMA2",
		  "MAD16 second (duplex) DMA",
		  FMT_INT,
		  0,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_MAD16), "MAD16_MPU_BASE",
		  "MAD16 MIDI I/O base",
		  FMT_HEX,
		  0x330,
		  "300, 310, 320 or 330 (0 disables)");

  ask_int_choice (B (OPT_MAD16), "MAD16_MPU_IRQ",
		  "MAD16 MIDI IRQ",
		  FMT_INT,
		  9,
		  "5, 7, 9 or 10");
  ask_int_choice (B (OPT_AUDIO), "DSP_BUFFSIZE",
		  "Audio DMA buffer size",
		  FMT_INT,
		  65536,
		  "4096, 16384, 32768 or 65536");
}

void
dump_script (void)
{
  int             i;

  for (i = 0; i <= OPT_LAST; i++)
    if (!(DUMMY_OPTS & B (i)))
      if (!(DISABLED_OPTIONS & B (i)))
	{
	  printf ("bool '%s' CONFIG_%s\n", questions[i], hw_table[i].macro);
	}

/*
 * Some "hardcoded" options
 */

  dump_only = 1;
  selected_options = 0;
  ask_parameters ();

  printf ("#\n$MAKE -C drivers/sound kernelconfig || exit 1\n");
}

void
dump_fixed_local (void)
{
  int             i = 0;

  printf ("/* Computer generated file. Please don't edit! */\n\n");
  printf ("#define KERNEL_COMPATIBLE_CONFIG\n\n");
  printf ("#define SELECTED_SOUND_OPTIONS\t0x%08x\n\n", selected_options);

  while (extra_options[i].name != NULL)
    {
      int             n = 0, j;

      printf ("#if ");

      for (j = 0; j < OPT_LAST; j++)
	if (!(DISABLED_OPTIONS & B (j)))
	  if (extra_options[i].mask & B (j))
	    {
	      if (n)
		printf (" || ");
	      if (!(n++ % 2))
		printf ("\\\n  ");

	      printf ("defined(CONFIG_%s)", hw_table[j].macro);
	    }

      printf ("\n");
      printf ("#\tdefine CONFIG_%s\n", extra_options[i].name);
      printf ("#endif\n\n");
      i++;
    }
}

void
dump_fixed_defines (void)
{
  int             i = 0;

  printf ("# Computer generated file. Please don't edit\n\n");

  while (extra_options[i].name != NULL)
    {
      int             j;

      for (j = 0; j < OPT_LAST; j++)
	if (!(DISABLED_OPTIONS & B (j)))
	  if (extra_options[i].mask & B (j))
	    {
	      printf ("ifdef CONFIG_%s\n", hw_table[j].macro);
	      printf ("CONFIG_%s=y\n", extra_options[i].name);
	      printf ("endif\n\n");
	    }

      i++;
    }
}

int
main (int argc, char *argv[])
{
  int             i, full_driver = 1;
  char            old_config_file[200];

  if (getuid () != 0)		/* Not root */
    {
      char           *home;

      if ((home = getenv ("HOME")) != NULL)
	{
	  sprintf (old_config_file, "%s/.soundconf", home);
	  oldconf = old_config_file;
	}
    }

  if (argc > 1)
    {
      if (strcmp (argv[1], "-o") == 0 &&
	  use_old_config (oldconf))
	exit (0);
      else if (strcmp (argv[1], "script") == 0)
	{
	  dump_script ();
	  exit (0);
	}
      else if (strcmp (argv[1], "fixedlocal") == 0)
	{
	  dump_fixed_local ();
	  exit (0);
	}
      else if (strcmp (argv[1], "fixeddefines") == 0)
	{
	  dump_fixed_defines ();
	  exit (0);
	}
    }

  fprintf (stderr, "\nConfiguring Sound Support\n\n");

  if (access (oldconf, R_OK) == 0)
    {
      char            str[255];

      sprintf (str, "Old configuration exists in `%s'. Use it", oldconf);
      if (think_positively (str, 1,
      "Enable this option to load the previously saved configuration file\n"
			    "for all of the sound driver parameters.\n"))
	if (use_old_config (oldconf))
	  exit (0);
    }

  printf ("/*\tGenerated by configure. Don't edit!!!!\t*/\n");
  printf ("/*\tMaking changes to this file is not as simple as it may look.\t*/\n\n");
  printf ("/*\tIf you change the CONFIG_ settings in local.h you\t*/\n");
  printf ("/*\t_have_ to edit .defines too.\t*/\n\n");

  {
    /*
     * Partial driver
     */

    full_driver = 0;

    for (i = 0; i <= OPT_LAST; i++)
      if (can_select_option (i))
	{
	  if (!(selected_options & B (i)))	/*
						 * Not selected yet
						 */
	    if (!hw_table[i].verify)
	      {
		if (hw_table[i].alias)
		  selected_options |= B (hw_table[i].alias);
		else
		  selected_options |= B (i);
	      }
	    else
	      {
		int             def_answ = hw_table[i].default_answ;

		if (think_positively (questions[i], def_answ, help[i]))
		  if (hw_table[i].alias)
		    selected_options |= B (hw_table[i].alias);
		  else
		    selected_options |= B (i);
	      }
	}
  }

  if (selected_options & B (OPT_SB))
    {
      if (think_positively (
			     "Support for the SG NX Pro mixer", 0,
       "Enable this if you want to support the additional mixer functions\n"
			  "provided on Sound Galaxy NX Pro sound cards.\n"))
	printf ("#define __SGNXPRO__\n");
    }

  if (selected_options & B (OPT_SB))
    {
      if (think_positively ("Support for the MV Jazz16 (ProSonic etc.)", 0,
	  "Enable this if you have an MV Jazz16 or ProSonic sound card.\n"))
	{
	  if (think_positively ("Do you have SoundMan Wave", 0,
				"Enable this option of you have the Logitech SoundMan Wave sound card.\n"))
	    {
	      printf ("#define SM_WAVE\n");

	    midi0001_again:
	      if (think_positively (
			   "Do you have access to the MIDI0001.BIN file", 1,
				     "The Logitech SoundMan Wave has a microcontroller which must be\n"
				     "initialized before MIDI emulation works. This is possible only if the\n"
			   "microcode file is compiled into the driver.\n"))
		{
		  char            path[512];

		  fprintf (stderr,
			   "Enter full name of the MIDI0001.BIN file (pwd is sound): ");
		  scanf ("%s", path);
		  fprintf (stderr, "including microcode file %s\n", path);

		  if (!bin2hex (path, "smw-midi0001.h", "smw_ucode"))
		    {
		      fprintf (stderr, "Couldn't open file %s\n",
			       path);
		      if (think_positively ("Try again with correct path", 1,
					    "The specified file could not be opened. Enter the correct path to the\n"
					    "file.\n"))
			goto midi0001_again;
		    }
		  else
		    {
		      printf ("#define SMW_MIDI0001_INCLUDED\n");
		      printf ("/*build bin2hex %s smw-midi0001.h smw_ucode */\n", path);
		    }
		}
	    }
	}
    }

  if (selected_options & B (OPT_SB))
    {
      if (think_positively ("Do you have a Logitech SoundMan Games", 0,
	 "The Logitech SoundMan Games supports 44 kHz in stereo while the\n"
      "standard SB Pro supports just 22 kHz stereo. You have the option of\n"
			    "enabling SM Games mode.  However, enable it only if you are sure that\n"
      "your card is an SM Games. Enabling this feature with a plain old SB\n"
			    "Pro will cause troubles with stereo mode.\n\n"
		  "DANGER! Read the above once again before answering 'y'\n"
			    "Answer 'n' if you are unsure what to do!\n"))
	printf ("#define SM_GAMES\n");
    }

  if (selected_options & B (OPT_AEDSP16))
    {
      int             sel1 = 0;

      if (selected_options & B (OPT_SB))
	{

	  if (think_positively (
	    "Do you want support for the Audio Excel SoundBlaster Pro mode",
				 1,
				 "Enable this option if you want the Audio Excel sound card to operate\n"
				 "in SoundBlaster Pro mode.\n"))
	    {
	      printf ("#define AEDSP16_SBPRO\n");
	      sel1 = 1;
	    }
	}

      if ((selected_options & B (OPT_MSS)) && (sel1 == 0))
	{

	  if (think_positively (
				 "Do you want support for the Audio Excel Microsoft Sound System mode",
				 1,
				 "Enable this option if you want the Audio Excel sound card to operate\n"
				 "in Microsoft Sound System mode.\n"))
	    {
	      printf ("#define AEDSP16_MSS\n");
	      sel1 = 1;
	    }
	}

      if (sel1 == 0)
	{
	  printf ("invalid_configuration__run_make_config_again\n");
	  fprintf (stderr, "ERROR!!!!!\nYou must select at least one mode when using Audio Excel!\n");
	  exit (-1);
	}
      if (selected_options & B (OPT_MPU401))
	printf ("#define AEDSP16_MPU401\n");
    }

  if (selected_options & B (OPT_PSS))
    {
    genld_again:
      if (think_positively ("Do you wish to include an LD file", 1,
			    "If you want to emulate the SoundBlaster card and you have a DSPxxx.LD\n"
		      "file then you must include the LD in the kernel.\n"))
	{
	  char            path[512];

	  fprintf (stderr,
		   "Enter the path to your LD file (pwd is sound): ");
	  scanf ("%s", path);
	  fprintf (stderr, "including LD file %s\n", path);

	  if (!bin2hex (path, "synth-ld.h", "pss_synth"))
	    {
	      fprintf (stderr, "couldn't open `%s' as the LD file\n", path);
	      if (think_positively ("try again with correct path", 1,
				    "The given LD file could not opened.\n"))
		goto genld_again;
	    }
	  else
	    {
	      printf ("#define PSS_HAVE_LD\n");
	      printf ("/*build bin2hex %s synth-ld.h pss_synth */\n", path);
	    }
	}
      else
	{
	  FILE           *sf = fopen ("synth-ld.h", "w");

	  fprintf (sf, "/* automaticaly generated by configure */\n");
	  fprintf (sf, "unsigned char pss_synth[1];\n"
		   "#define pss_synthLen 0\n");
	  fclose (sf);
	}
    }

  if (selected_options & B (OPT_TRIX))
    {
    hex2hex_again:

      if (think_positively ("Do you want to include TRXPRO.HEX in your kernel",
			    1,
	"The MediaTrix AudioTrix Pro has an onboard microcontroller which\n"
			    "needs to be initialized by downloading the code from the file TRXPRO.HEX\n"
			    "in the DOS driver directory. If you don't have the TRXPRO.HEX file handy\n"
			    "you may skip this step. However, the SB and MPU-401 modes of AudioTrix\n"
			    "Pro will not work without this file!\n"))
	{
	  char            path[512];

	  fprintf (stderr,
		 "Enter the path to your TRXPRO.HEX file (pwd is sound): ");
	  scanf ("%s", path);
	  fprintf (stderr, "including HEX file `%s'\n", path);

	  if (!hex2hex (path, "trix_boot.h", "trix_boot"))
	    goto hex2hex_again;
	  printf ("/*build hex2hex %s trix_boot.h trix_boot */\n", path);
	  printf ("#define INCLUDE_TRIX_BOOT\n");
	}
    }

  if (selected_options & B (OPT_MSS))
    {
      if (think_positively ("Support for builtin sound of Compaq Deskpro XL", 0,
			    "Enable this if you have Compaq Deskpro XL.\n"))
	{
	  printf ("#define DESKPROXL\n");
	}
    }

  if (selected_options & B (OPT_MAUI))
    {
    oswf_again:
      if (think_positively (
			     "Do you have access to the OSWF.MOT file", 1,
			     "TB Maui and Tropez have a microcontroller which needs to be initialized\n"
			     "prior use. OSWF.MOT is a file distributed with card's DOS/Windows drivers\n"
			     "which is required during initialization\n"))
	{
	  char            path[512];

	  fprintf (stderr,
		   "Enter full name of the OSWF.MOT file (pwd is sound): ");
	  scanf ("%s", path);
	  fprintf (stderr, "including microcode file %s\n", path);

	  if (!bin2hex (path, "maui_boot.h", "maui_os"))
	    {
	      fprintf (stderr, "Couldn't open file %s\n",
		       path);
	      if (think_positively ("Try again with correct path", 1,
				    "The specified file could not be opened. Enter the correct path to the\n"
				    "file.\n"))
		goto oswf_again;
	    }
	  else
	    {
	      printf ("#define HAVE_MAUI_BOOT\n");
	      printf ("/*build bin2hex %s maui_boot.h maui_os */\n", path);
	    }
	}
    }

  if (!(selected_options & ANY_DEVS))
    {
      printf ("invalid_configuration__run_make_config_again\n");
      fprintf (stderr, "\n*** This combination is useless. Sound driver disabled!!! ***\n*** You need to enable support for at least one device    ***\n\n");
      exit (0);
    }

  for (i = 0; i <= OPT_LAST; i++)
    if (!hw_table[i].alias)
      if (selected_options & B (i))
	printf ("#define CONFIG_%s\n", hw_table[i].macro);
      else
	printf ("#undef  CONFIG_%s\n", hw_table[i].macro);

  printf ("\n");

  i = 0;

  while (extra_options[i].name != NULL)
    {
      if (selected_options & extra_options[i].mask)
	printf ("#define CONFIG_%s\n", extra_options[i].name);
      else
	printf ("#undef  CONFIG_%s\n", extra_options[i].name);
      i++;
    }

  printf ("\n");

  ask_parameters ();

  printf ("#define SELECTED_SOUND_OPTIONS\t0x%08lx\n", selected_options);
  fprintf (stderr, "\nThe sound driver is now configured.\n");

#if defined(SCO) || defined(ISC) || defined(SYSV)
  fprintf (stderr, "Remember to update the System file\n");
#endif

  if (!old_config_used)
    {
      char            str[255];

      sprintf (str, "Save copy of this configuration to `%s'", oldconf);
      if (think_positively (str, 1,
	"If you enable this option then the sound driver configuration is\n"
      "saved to a file. If you later need to recompile the kernel you have\n"
			  "the option of using the saved configuration.\n"))
	{
	  char            cmd[200];

	  sprintf (cmd, "cp local.h %s", oldconf);

	  fclose (stdout);
	  if (system (cmd) != 0)
	    perror (cmd);
	}
    }
  exit (0);
}

int
bin2hex (char *path, char *target, char *varname)
{
  int             fd;
  int             count;
  char            c;
  int             i = 0;

  if ((fd = open (path, 0)) > 0)
    {
      FILE           *sf = fopen (target, "w");

      fprintf (sf, "/* automatically generated by configure */\n");
      fprintf (sf, "static unsigned char %s[] = {\n", varname);
      while (1)
	{
	  count = read (fd, &c, 1);
	  if (count == 0)
	    break;
	  if (i != 0 && (i % 10) == 0)
	    fprintf (sf, "\n");
	  fprintf (sf, "0x%02lx,", c & 0xFFL);
	  i++;
	}
      fprintf (sf, "};\n"
	       "#define %sLen %d\n", varname, i);
      fclose (sf);
      close (fd);
      return 1;
    }

  return 0;
}
