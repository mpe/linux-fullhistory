/* Capitalization rules for HPFS */

/* In OS/2, HPFS filenames preserve upper and lower case letter distinctions
   but filename matching ignores case.  That is, creating a file "Foo"
   actually creates a file named "Foo" which can be looked up as "Foo",
   "foo", or "FOO", among other possibilities.

   Also, HPFS is internationalized -- a table giving the uppercase
   equivalent of every character is stored in the filesystem, so that
   any national character set may be used.  If several different
   national character sets are in use, several tables are stored
   in the filesystem.

   It would be perfectly reasonable for Linux HPFS to act as a Unix
   filesystem and match "Foo" only if asked for "Foo" exactly.  But
   the sort order of HPFS directories is case-insensitive, so Linux
   still has to know the capitalization rules used by OS/2.  Because
   of this, it turns out to be more natural for us to be case-insensitive
   than not.

   Currently the standard character set used by Linux is Latin-1.
   Work is underway to permit people to use UTF-8 instead, therefore
   all code that depends on the character set is segregated here.

   (It would be wonderful if Linux HPFS could be independent of what
   character set is in use on the Linux side, but because of the
   necessary case folding this is impossible.)

   There is a map from Latin-1 into code page 850 for every printing
   character in Latin-1.  The NLS documentation of OS/2 shows that
   everybody has 850 available unless they don't have Western latin
   chars available at all (so fitting them to Linux without Unicode
   is a doomed exercise).

   It is not clear exactly how HPFS.IFS handles the situation when
   multiple code pages are in use.  Experiments show that

   - tables on the disk give uppercasing rules for the installed code pages

   - each directory entry is tagged with what code page was current
     when that name was created

   - doing just CHCP, without changing what's on the disk in any way,
     can change what DIR reports, and what name a case-folded match
     will match.

   This means, I think, that HPFS.IFS operates in the current code
   page, without regard to the uppercasing information recorded in
   the tables on the disk.  It does record the uppercasing rules
   it used, perhaps for CHKDSK, but it does not appear to use them
   itself.

   So: Linux, a Latin-1 system, will operate in code page 850.  We
   recode between 850 and Latin-1 when dealing with the names actually
   on the disk.  We don't use the uppercasing tables either.

   In a hypothetical UTF-8 implementation, one reasonable way to
   proceed that matches OS/2 (for least surprise) is: do case
   translation in UTF-8, and recode to/from one of the code pages
   available on the mounted filesystem.  Reject as invalid any name
   containing chars that can't be represented on disk by one of the
   code pages OS/2 is using.  Recoding from on-disk names to UTF-8
   could use the code page tags, though this is not what OS/2 does. */

static const unsigned char tb_cp850_to_latin1[128] =
{
  199, 252, 233, 226, 228, 224, 229, 231,
  234, 235, 232, 239, 238, 236, 196, 197,
  201, 230, 198, 244, 246, 242, 251, 249,
  255, 214, 220, 248, 163, 216, 215, 159,
  225, 237, 243, 250, 241, 209, 170, 186,
  191, 174, 172, 189, 188, 161, 171, 187,
  155, 156, 157, 144, 151, 193, 194, 192,
  169, 135, 128, 131, 133, 162, 165, 147,
  148, 153, 152, 150, 145, 154, 227, 195,
  132, 130, 137, 136, 134, 129, 138, 164,
  240, 208, 202, 203, 200, 158, 205, 206,
  207, 149, 146, 141, 140, 166, 204, 139,
  211, 223, 212, 210, 245, 213, 181, 254,
  222, 218, 219, 217, 253, 221, 175, 180,
  173, 177, 143, 190, 182, 167, 247, 184,
  176, 168, 183, 185, 179, 178, 142, 160,
};

#if 0
static const unsigned char tb_latin1_to_cp850[128] =
{
  186, 205, 201, 187, 200, 188, 204, 185,
  203, 202, 206, 223, 220, 219, 254, 242,
  179, 196, 218, 191, 192, 217, 195, 180,
  194, 193, 197, 176, 177, 178, 213, 159,
  255, 173, 189, 156, 207, 190, 221, 245,
  249, 184, 166, 174, 170, 240, 169, 238,
  248, 241, 253, 252, 239, 230, 244, 250,
  247, 251, 167, 175, 172, 171, 243, 168,
  183, 181, 182, 199, 142, 143, 146, 128,
  212, 144, 210, 211, 222, 214, 215, 216,
  209, 165, 227, 224, 226, 229, 153, 158,
  157, 235, 233, 234, 154, 237, 232, 225,
  133, 160, 131, 198, 132, 134, 145, 135,
  138, 130, 136, 137, 141, 161, 140, 139,
  208, 164, 149, 162, 147, 228, 148, 246,
  155, 151, 163, 150, 129, 236, 231, 152,
};
#endif

#define A_GRAVE 0300
#define THORN	0336   
#define MULTIPLY 0327
#define a_grave 0340
#define thorn	0376
#define divide	0367

static inline unsigned latin1_upcase (unsigned c)
{
  if (c - 'a' <= 'z' - 'a'
      || (c - a_grave <= thorn - a_grave
	  && c != divide))
    return c - 'a' + 'A';
  else
    return c;
}

static inline unsigned latin1_downcase (unsigned c)
{
  if (c - 'A' <= 'Z' - 'A'
      || (c - A_GRAVE <= THORN - A_GRAVE
	  && c != MULTIPLY))
    return c + 'a' - 'A';
  else
    return c;
}

#if 0
static inline unsigned latin1_to_cp850 (unsigned c)
{
  if ((signed) c - 128 >= 0)
    return tb_latin1_to_cp850[c - 128];
  else
    return c;
}
#endif

static inline unsigned cp850_to_latin1 (unsigned c)
{
  if ((signed) c - 128 >= 0)
    return tb_cp850_to_latin1[c - 128];
  else
    return c;
}

unsigned hpfs_char_to_upper_linux (unsigned c)
{
  return latin1_upcase (cp850_to_latin1 (c));
}

unsigned linux_char_to_upper_linux (unsigned c)
{
  return latin1_upcase (c);
}

unsigned hpfs_char_to_lower_linux (unsigned c)
{
  return latin1_downcase (cp850_to_latin1 (c));
}

unsigned hpfs_char_to_linux (unsigned c)
{
  return cp850_to_latin1 (c);
}
