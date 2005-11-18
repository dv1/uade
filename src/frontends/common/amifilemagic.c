/*
 Copyright (C) 2000-2005  Heikki Orsila
                          heikki.orsila@iki.fi
                          http://uade.ton.tut.fi
 Copyright (C) 2000-2005  Michael Doering

 This module is dual licensed under the GNU GPL and the Public Domain.
 Hence you may use _this_ module (not another code module) in any way you
 want in your projects.

 About security:

 This module tries to avoid any buffer overruns by not copying anything but
 hard coded strings (such as "FC13"). This doesn't
 copy any data from modules to program memory. Any memory writing with
 non-hard-coded data is an error by assumption. This module will only
 determine the format of a given module.

 Occasional memory reads over buffer ranges can occur, but they will of course
 be fixed when spotted :P The worst that can happen with reading over the
 buffer range is a core dump :)
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <amifilemagic.h>

static int chk_id_offset(unsigned char *buf, int bufsize,
			 const char *patterns[], int offset, char *pre);


/* Do not use '\0'. They won't work in patterns */
const char *offset_0000_patterns[] = {
  /* ID: Prefix: Desc: */
  "DIGI Booster", "DIGI",	/* Digibooster */
  "OKTASONG", "OKT",		/* Oktalyzer */
  "SYNTRACKER", "SYNMOD",	/* Syntracker */
  "OBISYNTHPACK", "OSP",	/* Synthpack */
  "SOARV1.0", "SA",		/* Sonic Arranger */
  "AON4", "AON4",              /* Art Of Noise (4ch) */
  "AON8", "AON8",              /* Art Of Noise (8ch) */
  "ARP.", "MTP2",       	/* HolyNoise / Major Tom */
  "AmBk", "ABK",		/* Amos ABK */
  "FUCO", "BSI",		/* FutureComposer BSI */
  "MMU2", "DSS",		/* DSS */
  "GLUE", "GLUE",		/* GlueMon */
  "ISM!", "IS",			/* In Stereo */
  "IS20", "IS20",		/* In Stereo 2 */
  "SMOD", "FC13",		/* FC 1.3 */
  "FC14", "FC14",		/* FC 1.4 */
  "MMDC", "MMDC",		/* Med packer */
  "MSOB", "MSO",		/* Medley */
  "MODU", "NTP",		/* Novotrade */
  "COSO", "HIPC",		/* Hippel Coso */
  "BeEp", "JAM",		/* Jamcracker */
  "ALL ", "DM1",		/* Deltamusic 1 */
  "YMST", "YM",			/* MYST ST-YM */
  "AMC ", "AMC",		/* AM-Composer */
  "P40A", "P40A",		/* The Player 4.0a */
  "P40B", "P40B",		/* The Player 4.0b */
  "P41A", "P41A",		/* The Player 4.1a */
  "P60A", "P60A",		/* The Player 6.0a */
  "SNT!", "PRU2",		/* Prorunner 2 */
  "MEXX_TP2", "TP2",		/* Tracker Packer 2 */
  "CPLX_TP3", "TP3",		/* Tracker Packer 3 */
  "MEXX", "TP1",		/* Tracker Packer 2 */
  "PM40", "PM40",		/* Promizer 4.0 */
  "FC-M", "FC-M",		/* FC-M */
  "E.M.S. V6.", "EMSV6",	/* EMS version 6 */
  "MCMD", "MCMD_org",		/* 0x00 MCMD format */
  "STP3", "STP3",		/* Soundtracker Pro 2 */
  "MTM", "MTM",			/* Multitracker */
  "Extended Module:", "XM",	/* Fasttracker2 */
  "MLEDMODL", "ML",		/* Musicline Editor */
  "YM!", "",                   /* stplay -- intentionally sabotaged */
  NULL, NULL
};

const char *offset_0024_patterns[] = {
  /* ID: Prefix: Desc: */
  "UNCLEART", "DL",		/* Dave Lowe WT */
  "DAVELOWE", "DL_deli",	/* Dave Lowe Deli */
  "J.FLOGEL", "JMF",		/* Janko Mrsic-Flogel */
  "BEATHOVEN", "BSS",		/* BSS */
  "FREDGRAY", "GRAY",		/* Fred Gray */
  "H.DAVIES", "HD",		/* Howie Davies */
  "RIFFRAFF", "RIFF",		/* Riff Raff */
  "!SOPROL!", "SPL",		/* Soprol */
  "F.PLAYER", "FP",		/* F.Player */
  "S.PHIPPS", "CORE",		/* Core Design */
  "DAGLISH!", "BDS",		/* Benn Daglish */
  NULL, NULL
};


/* check for 'pattern' in 'buf'.
   the 'pattern' must lie inside range [0, maxlen) in the buffer.
   returns true if pattern is at buf[offset], otherwrise false
 */
static int patterntest(const char *buf, const char *pattern,
		       int offset, int bytes, int maxlen)
{
  if ((offset + bytes) <= maxlen) {
    return memcmp(buf + offset, pattern, bytes) ? 0 : 1;
  }
  fprintf(stderr,
	  "uade: warning: would have searched filemagic outside of range\n");
  return 0;
}

static int tronictest(unsigned char *buf, int bufsize)
{
  int a = 0;

  a = ((buf[0x02] << 8) + buf[0x03]) + ((buf[0x06] << 8) + buf[0x07]) +
      ((buf[0x0a] << 8) + buf[0x0b]) + ((buf[0x0e] << 8) + buf[0x0f]) + 0x10;

  if ((a + 1 > bufsize) || (a & 1 << 0))
    return 0;			/* size  & btst #0, d1; */

  a = ((buf[a] << 8) + buf[a + 1]) + a;
  if ((a + 7 > bufsize) || (a & 1 << 0))
    return 0;			/*size & btst #0,d1 */

  if ((((buf[a + 4] << 24) + (buf[a + 5] << 16) +
	(buf[a + 6] << 8) + buf[a + 7]) != 0x5800b0))
    return 0;

  return 1;
}

static int tfmxtest(unsigned char *buf, int bufsize, char *pre)
{

  int ret = 0;

  if (buf[0] == 'T' && buf[1] == 'F' && buf[2] == 'H' && buf[3] == 'D') {
    if (buf[0x8] == 0x01) {
      strcpy(pre, "TFHD1.5");	/* One File TFMX format by Alexis NASR */
      ret = 1;
    } else if (buf[0x8] == 0x02) {
      strcpy(pre, "TFHDPro");
      ret = 1;
    } else if (buf[0x8] == 0x03) {
      strcpy(pre, "TFHD7V");
      ret = 1;
    }

  } else
      if ((buf[0] == 'T' && buf[1] == 'F' && buf[2] == 'M'
	   && buf[3] == 'X') || (buf[0] == 't' && buf[1] == 'f'
				 && buf[2] == 'm' && buf[3] == 'x')) {


    if ((buf[4] == '-' && buf[5] == 'S' && buf[6] == 'O' && buf[7] == 'N'
	 && buf[8] == 'G') || (buf[4] == '_' && buf[5] == 'S'
			       && buf[6] == 'O' && buf[7] == 'N'
			       && buf[8] == 'G' && buf[9] == ' ')
	|| (buf[4] == 'S' && buf[5] == 'O' && buf[6] == 'N'
	    && buf[7] == 'G') || (buf[4] == 's' && buf[5] == 'o'
				  && buf[6] == 'n' && buf[7] == 'g')
	|| (buf[4] == 0x20)) {

      strcpy(pre, "MDAT");	/*default TFMX: TFMX Pro */
      ret = 1;

      if ((buf[10] == 'b' && buf[11] == 'y') || (buf[16] == ' ' && buf[17] == ' ') || (buf[16] == '(' && buf[17] == 'E' && buf[18] == 'm' && buf[19] == 'p' && buf[20] == 't' && buf[21] == 'y' && buf[22] == ')') || (buf[16] == 0x30 && buf[17] == 0x3d) ||	/*lethal Zone */
	  (buf[4] == 0x20)) {
	if (buf[464] == 0x00 && buf[465] == 0x00 && buf[466] == 0x00
	    && buf[467] == 0x00) {
	  if ((buf[14] != 0x0e && buf[15] != 0x60) ||	/*z-out title */
	      (buf[14] == 0x08 && buf[15] == 0x60 && buf[4644] != 0x09 && buf[4645] != 0x0c) ||	/* metal law */
	      (buf[14] == 0x0b && buf[15] == 0x20 && buf[5120] != 0x8c && buf[5121] != 0x26) ||	/* bug bomber */
	      (buf[14] == 0x09 && buf[15] == 0x20 && buf[3876] != 0x93 && buf[3977] != 0x05)) {	/* metal preview */
	    strcpy(pre, "TFMX1.5");	/*TFMX 1.0 - 1.6 */
	  }
	}
      } else if (((buf[0x0e] == 0x08 && buf[0x0f] == 0xb0) &&	/* BMWi */
		  (buf[0x140] == 0x00 && buf[0x141] == 0x0b) &&	/*End tackstep 1st subsong */
		  (buf[0x1d2] == 0x02 && buf[0x1d3] == 0x00) &&	/*Trackstep datas */
		  (buf[0x200] == 0xff && buf[0x201] == 0x00 &&	/*First effect */
		   buf[0x202] == 0x00 && buf[0x203] == 0x00 &&
		   buf[0x204] == 0x01 && buf[0x205] == 0xf4 &&
		   buf[0x206] == 0xff && buf[0x207] == 0x00))
		 || ((buf[0x0e] == 0x0A && buf[0x0f] == 0xb0) && /* B.C Kid */
		     (buf[0x140] == 0x00 && buf[0x141] == 0x15) && /*End tackstep 1st subsong */
		     (buf[0x1d2] == 0x02 && buf[0x1d3] == 0x00) && /*Trackstep datas */
		     (buf[0x200] == 0xef && buf[0x201] == 0xfe &&	/*First effect */
		      buf[0x202] == 0x00 && buf[0x203] == 0x03 &&
		      buf[0x204] == 0x00 && buf[0x205] == 0x0d &&
		      buf[0x206] == 0x00 && buf[0x207] == 0x00))) {
	strcpy(pre, "TFMX7V");	/* "special cases TFMX 7V */
      } else {

	int e, i, s, t;
	
	/* Trackstep datas offset */
	if (buf[0x1d0] == 0x00 && buf[0x1d1] == 0x00 && buf[0x1d2] == 0x00
	    && buf[0x1d3] == 0x00) {
	  /* unpacked */
	  s = 0x00000800;
	} else {
	  /*packed */
	  s = (buf[0x1d0] << 24) + (buf[0x1d1] << 16) + (buf[0x1d2] << 8) + buf[0x1d3];	/*packed */
	}

	for (i = 0; i < 0x3d; i += 2) {
	  if (((buf[0x140 + i] << 8) + buf[0x141 + i]) > 0x00) {	/*subsong */
	    t = (((buf[0x100 + i] << 8) + (buf[0x101 + i])) * 16 + s);	/*Start of subsongs Trackstep data :) */
	    e = (((buf[0x140 + i] << 8) + (buf[0x141 + i])) * 16 + s);	/*End of subsongs Trackstep data :) */
	    if (t < bufsize || e < bufsize) {
	      for (t = t; t < e; t += 2) {
		if (buf[t] == 0xef && buf[t + 1] == 0xfe) {
		  if (buf[t + 2] == 0x00 && buf[t + 3] == 0x03 &&
		      buf[t + 4] == 0xff && buf[t + 5] == 0x00
		      && buf[t + 6] == 0x00) {
		    i = 0x3d;
		    strcpy(pre, "TFMX7V");	/*TFMX 7V */
		    break;
		  }
		}
	      }
	    }
	  }
	}
      }
    }
  }
  return ret;
}

/* returns:	 -1 for a mod with bad length 		*/
/* 		 0  for no mod				*/
/*		 1 for a mod with good length		*/
static int modlentest(unsigned char *buf, int filesize, int header)
{
  int i = 0;
  int no_of_instr;
  int smpl = 0;
  int plist;
  int maxpattern = 0;

  if (header == 600)   {
    no_of_instr = 15;
    plist = header - 130;
    } else {
    header = 1084;
    no_of_instr = 31;
    plist = header -4 - 130 ;
    }
    
  if (header > filesize)
    return 0;			/* no mod */

  if (buf[43] + no_of_instr * 30 > filesize)
    return 0;			/* no mod */

  for (i = 0; i < 128; i++) {
    if (buf[plist + 2 + i] > maxpattern)
      maxpattern = buf[plist + 2 + i];
  }

  if (maxpattern > 100) return 0;

  for (i = 0; i < no_of_instr; i++) {
    smpl = smpl + (buf[42 + i * 30] << 8) + buf[43 + i * 30];	/* smpl len  */
  }
 
  if (filesize < (header + (maxpattern + 1) * 1024 + smpl * 2)){
    return -1; 			/*size  error */
  } else {
    return 1;			/*size ok, sort of */
  }

  if (filesize > (header + (maxpattern + 1) * 1024 + smpl * 2) + 1024) {
    return -1;			/*size error */
  } else {
    return 1;			/*size ok, sort of */
  }
  return 0;
}

static void modparsing(unsigned char *buf, int bufsize, int header, int max_pattern, int pfx[])
{

    int offset=0;
    int i,j,fx;
    unsigned char fxarg;
    

    if ((header+256*4+(max_pattern)*1024 > bufsize)) {
	fprintf (stderr, "***Warning*** this your friendly amifilemagic Soundtracker check routine\n");
	fprintf (stderr, "              buffer too small for checking the whole music data: %d/%d\n",600+256*4+(max_pattern+1)*1024,bufsize);
	fprintf (stderr, "              overide replayer with -P <replayer> if neccessary!\n");
        max_pattern=(bufsize-header-256*4)/1024;
	fprintf (stderr, "              just checking %d patterns now...\n\n",max_pattern);	
    }

    for ( i=0; i<= max_pattern; i++ )
    {
     for (j=0; j<256; j++ )
     {
     offset = header+j*4+i*1024;
     
     fx = buf[offset+2] & 0x0f;
     fxarg = buf[offset+3];

     switch (fx)
     { 
        case 0:
	  if (fxarg != 0 )
	   pfx[fx] += 1;
           break;
         case 1:
         case 2:
         case 3:
         case 4:
         case 5:
         case 6:
         case 7:
         case 8:
         case 9:
         case 10:
         case 11:
         case 12:
         case 13:
          pfx[fx] +=1;
          break;
         case 14: // 0x0e Extended Commands//
          pfx[((fxarg>>4)&0x0f) + 16] +=1;
          break;
         case 15: //0x0f set Tempo/Set Speed
          if (fxarg > 0x1f)
           pfx[14] +=1;
          else
           pfx[15] +=1;
           break;
	  }
     } 
    }
/* print fx list for debugging */
/*
    for (j=0; j<32; j++ )
     {
      fprintf (stderr, "effects: %d\t%d\n",j, pfx[j]);
     }
*/

return;
}


static int mod32check(unsigned char *buf, int bufsize, int realfilesize)
/* returns:	 0 for undefined                            */
/* 		 1 for a Soundtracker 32instr.		    */
/*		 2 for a Noisetracker 1.0		    */
/*		 3 for a Noisetracker 2.0		    */
/*		 4 for a Startrekker 4ch		    */
/*		 5 for a Startrekker 8ch		    */
/*		 6 for Audiosculpture 4 ch/fm		    */
/*		 7 for Audiosculpture 8 ch/fm		    */
/*		 8 for a Protracker 			    */
/*		 9 for a Fasttracker			    */
/*		 10 for a Noisetracker (M&K!)		    */
{
/* todo: port and enhance ptk-prowiz detection to amifilemagic */

    /* mod patterns at file offset 0x438 */
    char *mod_patterns[] = { "M.K.", ".M.K", 0 };
    /* startrekker patterns at file offset 0x438 */
    char *startrekker_patterns[] = { "FLT4", "FLT8", "EXO4", "EXO8", 0 };

    int max_pattern=0;
    int i,j,t,ret;
    int pfx[32];


    /* Special cases first */
    if (patterntest(buf, "M&K!", 0x438, 4, bufsize)) {
      return 10;	/* Noisetracker (M&K!) */
      }

    if (patterntest(buf, "M!K!", 0x438, 4, bufsize)) {
      return 8;		/* Protracker (100 patterns) */
      }

    if (patterntest(buf, "N.T.", 0x438, 4, bufsize)) {
      return 3;		/* Noisetracker2.x */
      }

    for (i = 0; startrekker_patterns[i]; i++) {
     if (patterntest(buf, startrekker_patterns[i], 0x438, 4, bufsize)) {
      t = 0;
      for (j = 0; j < 30 * 0x1e; j = j + 0x1e) {
	if (buf[0x2a + j] == 0 && buf[0x2b + j] == 0 && buf[0x2d + j] != 0) {
	  t = t + 1;		/* no of AM instr. */
	}
      }
      if (t > 0) {
	if (buf[0x43b] == '4'){
		ret=6;			/* Startrekker 4 AM / ADSC */
	    } else { 		
		ret=7;			/* Startrekker 8 AM / ADSC */	
	    }
      } else {
	if (buf[0x43b] == '4'){
		ret=4;			/* Startrekker 4ch */
	    } else { 		
		ret=5;			/* Startrekker 8ch */	
	    }
	}
      return ret;
      }
    }

    for (i = 0; mod_patterns[i]; i++) {
     if (patterntest(buf, mod_patterns[i], 0x438, 4, bufsize)) {
	/* seems to be a generic M.K. MOD                              */
	/* TODO: DOC Soundtracker, Noisetracker 1.0 & 2.0, Protracker  */
	/*       and Fasttracker checking                               */

	if (modlentest(buf, realfilesize, 1084) <1 ) return 0; /* modlentest failed */

	for (i = 0; i < 128; i++) {
    	     max_pattern=(buf[1080 - 130 + 2 + i] > max_pattern) ? buf[1080 - 130 + 2 + i] : max_pattern;
	    }

	    if (max_pattern > 100) return 0;		/* pattern number can only be  0 <-> 63 for mod15*/

	memset (pfx,0,sizeof (pfx));
	modparsing(buf, bufsize, 1084-4, max_pattern, pfx);
	
	for (j=17; j<=31; j++)
    	 {
    	   if (pfx[j] != 0) 
    	  	{
    		return 8; /* Definetely Pro or Fastracker - extended effects used*/
    		}
    	 }
    	//return 3; // noisetracker
      }
    }
return 0;
}


static int mod15check(unsigned char *buf, int bufsize, int realfilesize)
/* pattern parsing based on Sylvain 'Asle' Chipaux'	*/
/* Modinfo-V2						*/
/*							*/
/* returns:	 0 for an undefined mod 		*/
/* 		 1 for a DOC Soundtracker mod		*/
/*		 2 for a Ultimate ST mod		*/
/*		 3 for a Mastersoundtracker		*/
/*		 4 for a SoundtrackerV2.0 -V4.0		*/
{
  int i = 0, j = 0;
  int slen = 0;
  int srep = 0;
  int sreplen = 0;
  int vol = 0;
  int ret = 0;

  int noof_slen_zero_sreplen_zero=0;
  int noof_slen_zero_sreplen_nonzero=0;
  int noof_slen_zero_vol_zero=0;
  int srep_bigger_slen=0;
  int srep_bigger_ffff=0;
  int st_xy=0;
  
  int max_pattern=1;
  int pfx[32];

  /* sanity checks */
  if (bufsize < 0x1f3)
    return 0;			/* file too small */
  if (bufsize < 2648+4 || realfilesize <2648+4) /* size 1 pattern + 1x 4 bytes Instrument :) */
    return 0;

  if (modlentest(buf, realfilesize, 600) < 1) return 0; /* modlentest failed */

 /* check for 15 instruments */
  if (buf[0x1d6] != 0x00 && buf[0x1d6] < 0x7f) {
    for (i = 0; i < 128; i++) {	/* pattern list table: 128 posbl. entries */
      max_pattern=(buf[600 - 130 + 2 + i] > max_pattern) ? buf[600 - 130 + 2 + i] : max_pattern;
    }
    if (max_pattern > 63) return 0;		/* pattern number can only be  0 <-> 63 for mod15*/
  } else {
    return 0;
  }

 /* parse instruments */
    for (i = 0; i < 15; i++) {
      vol = buf[45 + i * 30];
      slen = ((buf[42 + i * 30] << 8) + buf[43 + i * 30]) * 2;
      srep = ((buf[46 + i * 30] << 8) + buf[47 + i * 30]);
      sreplen = ((buf[48 + i * 30] << 8) + buf[49 + i * 30]) * 2;
//      fprintf (stderr, "%d, slen: %d, %d (srep %d, sreplen %d), vol: %d\n",i, slen, srep+sreplen,srep, sreplen, vol);

      if (slen == 0) {
       if  (vol == 0 )
        {  noof_slen_zero_vol_zero++;} 
       if  (sreplen == 0 ) {
          noof_slen_zero_sreplen_zero++;
	} else {
	  noof_slen_zero_sreplen_nonzero++;}
       } else {
            if ((srep+sreplen) > slen)
	    srep_bigger_slen++;
       }
       	
	/* slen < 9999 */
	slen = (buf[42 + i * 30] << 8) + buf[43 + i * 30];
	if (slen <= 9999) {
	  /* repeat offset + repeat size*2 < word size */
	  srep = ((buf[48 + i * 30] << 8) + buf[49 + i * 30]) * 2 +
	      ((buf[46 + i * 30] << 8) + buf[47 + i * 30]);
	  if (srep > 0xffff) srep_bigger_ffff++;
        }

	if  (buf[25+i*30] ==':' && buf [22+i*30] == '-' &&
	   ((buf[20+i*30] =='S' && buf [21+i*30] == 'T') ||
	    (buf[20+i*30] =='s' && buf [21+i*30] == 't'))) st_xy++;
    }

/* parse pattern data -> fill pfx[] with number of times fx being used*/
    memset (pfx,0,sizeof (pfx));
    modparsing(buf, bufsize, 600, max_pattern, pfx);

/* and now for let's see if we can spot the mod */

/* FX used:					*/
/* Ultimate ST:			0,1,2		*/
/* MasterSoundtracker:		0,1,2,  c,  e,f	*/
/* DOC-Soundtracker V2.0:	0,1,2,b,c,d,e,f */
/* Soundtracker II-IV		0,1,2,3,4,5,6,7,8,9,a,b,c,d,e,f*/


/* Check for instruments used between 0x3 <-> 0xb */
   for (j=3; j<0xc; j++ )
     {
      if (pfx[j] !=0)
       { return 4; /* Most likely one of those weird ST II-IV mods*/ }
     }

   for (j=0xb; j<0x10; j++)
     {
     if (pfx[j] != 0) 
      {
       ret=1; /* most likely  a DOC ST or MasterSoundtracker */
       break;
      }
     }

    if (ret == 1)
     {
     if (pfx[0x0d] > max_pattern)
	 {
          return 4; /* Most likely one of those weird ST II-IV mods*/
	  }
      if ((pfx[0x0b] == 0 || pfx[0x0d] == 0) && (noof_slen_zero_sreplen_zero !=0 && noof_slen_zero_sreplen_nonzero ==0 && st_xy==0))
        {
        return 3;	/* Master ST */
      } else {
        return 1;	/* DOC ST */
      }
    }
       
    if (srep_bigger_slen == 0 && srep_bigger_ffff == 0 && pfx[0x00] ==0 &&
        ((st_xy != 0 && buf[0x1d7] != 120 ) || st_xy==0))
    {
     return 2;		/* can savely be played as Ultimate ST */
    }
  return 0;
}


void filemagic(unsigned char *buf, char *pre, int realfilesize)
{
  /* char filemagic():
     detects formats like e.g.: tfmx1.5, hip, hipc, fc13, fc1.4      
     - tfmx 1.5 checking based on both tfmx DT and tfmxplay by jhp, 
     and the EP by Don Adan/WT.
     - tfmx 7v checking based on info by don adan, the amore file
     ripping description and jhp's desc of the tfmx format.
     - other checks based on e.g. various player sources from Exotica 
     or by checking bytes with a hexeditor
     by far not complete...

     NOTE: Those Magic ID checks are quite lame compared to the checks the
     amiga replayer do... well, after all we are not ripping. so they
     have to do at the moment :)
   */

  int i,t;
  const int bufsize = 8192;

  t = mod32check(buf, bufsize, realfilesize);
    if (t >0)
    {
     switch (t)
     { 
         case 1:
    	    strcpy(pre, "MOD_DOC");	/* Soundtracker 32instrument*/
	    break;
         case 2:
    	    strcpy(pre, "MOD_NTK1");	/* Noisetracker 1.x*/
	    break;
         case 3:
    	    strcpy(pre, "MOD_NTK2");	/* Noisetracker 2.x*/
	    break;
         case 4:
    	    strcpy(pre, "MOD_FLT4");	/* Startrekker 4ch*/
	    break;
         case 5:
    	    strcpy(pre, "MOD_FLT8");	/* Startrekker 8ch*/
	    break;
         case 6:
    	    strcpy(pre, "MOD_ADSC4");	/* Audiosculpture 4ch AM*/
	    break;
         case 7:
    	    strcpy(pre, "MOD_ADSC8");	/* Audiosculpture 8ch AM*/
	    break;
         case 9:
    	    strcpy(pre, "MOD_FT1");	/* Fasttracker 4 ch*/
	    break;
         case 8:
    	    strcpy(pre, "MOD");		/* Protracker*/
	    break;
         case 10:
    	    strcpy(pre, "MOD_NTKAMP");	/* Noisetracker (M&K!)*/
	    break;
	  }
	return;
    }
  
  
  t = mod15check(buf, bufsize, realfilesize);
  if (t > 0) {
     strcpy(pre, "MOD15");	/* normal Soundtracker 15 */
     if (t == 2) {
      strcpy(pre, "MOD15_UST");	/* Ultimate ST */
     }
     if (t == 3) {
      strcpy(pre, "MOD15_MST");	/* Mastersoundtracker */
     }
     if (t == 4) {
      strcpy(pre, "MOD15_ST-IV");	/* Soundtracker iV */
     }
     return;
    }
 

  if (((buf[0x438] >= '1' && buf[0x438] <= '3')
       && (buf[0x439] >= '0' && buf[0x439] <= '9') && buf[0x43a] == 'C'
       && buf[0x43b] == 'H') || ((buf[0x438] >= '2' && buf[0x438] <= '8')
				 && buf[0x439] == 'C' && buf[0x43a] == 'H'
				 && buf[0x43b] == 'N')
      || (buf[0x438] == 'T' && buf[0x439] == 'D' && buf[0x43a] == 'Z')
      || (buf[0x438] == 'O' && buf[0x439] == 'C' && buf[0x43a] == 'T'
	  && buf[0x43b] == 'A') || (buf[0x438] == 'C' && buf[0x439] == 'D'
				    && buf[0x43a] == '8'
				    && buf[0x43b] == '1')) {
    strcpy(pre, "MOD_XCHN");	/*Multichannel Tracker */
    
  } else if (buf[0x2c] == 'S' && buf[0x2d] == 'C' && buf[0x2e] == 'R'
	     && buf[0x2f] == 'M') {
    strcpy(pre, "S3M");		/*Scream Tracker */
    
  } else if ((buf[0] == 0x60 && buf[2] == 0x60 && buf[4] == 0x48
	      && buf[5] == 0xe7) || (buf[0] == 0x60 && buf[2] == 0x60
				     && buf[4] == 0x41 && buf[5] == 0xfa)
	     || (buf[0] == 0x60 && buf[1] == 0x00 && buf[4] == 0x60
		 && buf[5] == 0x00 && buf[8] == 0x48 && buf[9] == 0xe7)
	     || (buf[0] == 0x60 && buf[1] == 0x00 && buf[4] == 0x60
		 && buf[5] == 0x00 && buf[8] == 0x60 && buf[9] == 0x00
		 && buf[12] == 0x60 && buf[13] == 0x00 && buf[16] == 0x48
		 && buf[17] == 0xe7)) {
    strcpy(pre, "HIP");		/* Hippel */
    
  } else if (buf[0x348] == '.' && buf[0x349] == 'Z' && buf[0x34A] == 'A'
	     && buf[0x34B] == 'D' && buf[0x34c] == 'S' && buf[0x34d] == '8'
	     && buf[0x34e] == '9' && buf[0x34f] == '.') {
    strcpy(pre, "MKII");	/* Mark II */
    
  } else if (((buf[0] == 0x08 && buf[1] == 0xf9 && buf[2] == 0x00
	       && buf[3] == 0x01) && (buf[4] == 0x00 && buf[5] == 0xbb
				      && buf[6] == 0x41 && buf[7] == 0xfa)
	      && ((buf[0x25c] == 0x4e && buf[0x25d] == 0x75)
		  || (buf[0x25c] == 0x4e && buf[0x25d] == 0xf9)))
	     || ((buf[0] == 0x41 && buf[1] == 0xfa)
		 && (buf[4] == 0xd1 && buf[5] == 0xe8)
		 && (((buf[0x230] == 0x4e && buf[0x231] == 0x75)
		      || (buf[0x230] == 0x4e && buf[0x231] == 0xf9))
		     || ((buf[0x29c] == 0x4e && buf[0x29d] == 0x75)
			 || (buf[0x29c] == 0x4e && buf[0x29d] == 0xf9))
		     ))) {
    strcpy(pre, "SID1");	/* SidMon1 */

  } else if (buf[0] == 0x4e && buf[1] == 0xfa &&
	     buf[4] == 0x4e && buf[5] == 0xfa &&
	     buf[8] == 0x4e && buf[9] == 0xfa &&
	     buf[0xc] == 0x4e && buf[0xd] == 0xfa) {
    for (i = 0x10; i < 256; i = i + 2) {
      if (buf[i + 0] == 0x4e && buf[i + 1] == 0x75 && buf[i + 2] == 0x47
	  && buf[i + 3] == 0xfa && buf[i + 12] == 0x4e && buf[i + 13] == 0x75) {
	strcpy(pre, "FRED");	/* FRED */
	break;
      }
    }

  } else if (buf[0] == 0x60 && buf[1] == 0x00 &&
	     buf[4] == 0x60 && buf[5] == 0x00 &&
	     buf[8] == 0x60 && buf[9] == 0x00 &&
	     buf[12] == 0x48 && buf[13] == 0xe7) {
    strcpy(pre, "MA");		/*Music Assembler */

  } else if (buf[0] == 0x00 && buf[1] == 0x00 &&
	     buf[2] == 0x00 && buf[3] == 0x28 &&
	     (buf[7] >= 0x34 && buf[7] <= 0x64) &&
	     buf[0x20] == 0x21 && (buf[0x21] == 0x54 || buf[0x21] == 0x44)
	     && buf[0x22] == 0xff && buf[0x23] == 0xff) {
    strcpy(pre, "SA-P");	/*SonicArranger Packed */

  } else if (buf[0] == 0x4e && buf[1] == 0xfa &&
	     buf[4] == 0x4e && buf[5] == 0xfa &&
	     buf[8] == 0x4e && buf[9] == 0xfa) {
    t = ((buf[2] * 256) + buf[3]);
    if (t < bufsize - 9) {
      if (buf[2 + t] == 0x4b && buf[3 + t] == 0xfa &&
	  buf[6 + t] == 0x08 && buf[7 + t] == 0xad && buf[8 + t] == 0x00
	  && buf[9 + t] == 0x00) {
	strcpy(pre, "MON");	/*M.O.N */
      }
    }

  } else if (buf[0] == 0x02 && buf[1] == 0x39 &&
	     buf[2] == 0x00 && buf[3] == 0x01 &&
	     buf[8] == 0x66 && buf[9] == 0x02 &&
	     buf[10] == 0x4e && buf[11] == 0x75 &&
	     buf[12] == 0x78 && buf[13] == 0x00 &&
	     buf[14] == 0x18 && buf[15] == 0x39) {
    strcpy(pre, "MON_old");	/*M.O.N_old */

  } else if (buf[0] == 0x48 && buf[1] == 0xe7 && buf[2] == 0xf1
	     && buf[3] == 0xfe && buf[4] == 0x61 && buf[5] == 0x00) {
    t = ((buf[6] * 256) + buf[7]);
    if (t < (bufsize - 17)) {
      for (i = 0; i < 10; i = i + 2) {
	if (buf[6 + t + i] == 0x47 && buf[7 + t + i] == 0xfa) {
	  strcpy(pre, "DW");	/*Whittaker Type1... FIXME: incomplete */
	}
      }
    }

  } else if (buf[0] == 0x13 && buf[1] == 0xfc &&
	     buf[2] == 0x00 && buf[3] == 0x40 &&
	     buf[8] == 0x4e && buf[9] == 0x71 &&
	     buf[10] == 0x04 && buf[11] == 0x39 &&
	     buf[12] == 0x00 && buf[13] == 0x01 &&
	     buf[18] == 0x66 && buf[19] == 0xf4 &&
	     buf[20] == 0x4e && buf[21] == 0x75 &&
	     buf[22] == 0x48 && buf[23] == 0xe7 &&
	     buf[24] == 0xff && buf[25] == 0xfe) {
    strcpy(pre, "EX");		/*Fashion Tracker */

/* Magic ID */
  } else if (buf[0x3a] == 'S' && buf[0x3b] == 'I' && buf[0x3c] == 'D' &&
	     buf[0x3d] == 'M' && buf[0x3e] == 'O' && buf[0x3f] == 'N' &&
	     buf[0x40] == ' ' && buf[0x41] == 'I' && buf[0x42] == 'I') {
    strcpy(pre, "SID2");	/* SidMon II */

  } else if (buf[0x28] == 'R' && buf[0x29] == 'O' && buf[0x2a] == 'N' &&
	     buf[0x2b] == '_' && buf[0x2c] == 'K' && buf[0x2d] == 'L' &&
	     buf[0x2e] == 'A' && buf[0x2f] == 'R' && buf[0x30] == 'E' &&
	     buf[0x31] == 'N') {
    strcpy(pre, "RK");		/* Ron Klaren (CustomMade) */

  } else if (buf[0x3e] == 'A' && buf[0x3f] == 'C' && buf[0x40] == 'T'
	     && buf[0x41] == 'I' && buf[0x42] == 'O' && buf[0x43] == 'N'
	     && buf[0x44] == 'A' && buf[0x45] == 'M') {
    strcpy(pre, "AST");		/*Actionanamics */

  } else if (buf[26] == 'V' && buf[27] == '.' && buf[28] == '2') {
    strcpy(pre, "BP");		/* Soundmon V2 */

  } else if (buf[26] == 'V' && buf[27] == '.' && buf[28] == '3') {
    strcpy(pre, "BP3");		/* Soundmon V2.2 */

  } else if (buf[60] == 'S' && buf[61] == 'O' && buf[62] == 'N'
	     && buf[63] == 'G') {
    strcpy(pre, "SFX13");	/* Sfx 1.3-1.8 */

  } else if (buf[124] == 'S' && buf[125] == 'O' && buf[126] == 'N'
	     && buf[127] == 'G') {
    strcpy(pre, "SFX20");	/* Sfx 2.0 */

  } else if (buf[0x1a] == 'E' && buf[0x1b] == 'X' && buf[0x1c] == 'I'
	     && buf[0x1d] == 'T') {
    strcpy(pre, "AAM");		/*Audio Arts & Magic */
  } else if (buf[8] == 'E' && buf[9] == 'M' && buf[10] == 'O'
	     && buf[11] == 'D' && buf[12] == 'E' && buf[13] == 'M'
	     && buf[14] == 'I' && buf[15] == 'C') {
    strcpy(pre, "EMOD");	/* EMOD */

    /* generic ID Check at offset 0x24 */

  } else if (chk_id_offset(buf, bufsize, offset_0024_patterns, 0x24, pre)) {

    /* HIP7 ID Check at offset 0x04 */
  } else if (patterntest(buf, " **** Player by Jochen Hippel 1990 **** ",
			 0x04, 40, bufsize)) {
    strcpy(pre, "HIP7");	/* HIP7 */

    /* Magic ID at Offset 0x00 */
  } else if (buf[0] == 'M' && buf[1] == 'M' && buf[2] == 'D') {
    if (buf[0x3] >= '0' && buf[0x3] < '3') {
      /*move.l mmd_songinfo(a0),a1 */
      int s = (buf[8] << 24) + (buf[9] << 16) + (buf[0xa] << 8) + buf[0xb];
      if (((int) buf[s + 767]) & (1 << 6)) {	/* btst #6, msng_flags(a1); */
	strcpy(pre, "OCTAMED");
       /*OCTAMED*/} else {
	strcpy(pre, "MED");
       /*MED*/}
    } else if (buf[0x3] != 'C') {
      strcpy(pre, "MMD3");	/* mmd3 and above */
    }

    /* all TFMX format tests here */
  } else if (tfmxtest(buf, bufsize, pre)) {
    /* is TFMX, nothing to do here ('pre' set in tfmxtest() */

  } else if (buf[0] == 'T' && buf[1] == 'H' && buf[2] == 'X') {
    if ((buf[3] == 0x00) || (buf[3] == 0x01)) {
      strcpy(pre, "AHX");	/* AHX */
    }

  } else if (buf[1] == 'M' && buf[2] == 'U' && buf[3] == 'G'
	     && buf[4] == 'I' && buf[5] == 'C' && buf[6] == 'I'
	     && buf[7] == 'A' && buf[8] == 'N') {
    if (buf[9] == '2') {
      strcpy(pre, "MUG2");	/* Digimugi2 */
    } else {
      strcpy(pre, "MUG");	/* Digimugi */
    }

  } else if (buf[0] == 'L' && buf[1] == 'M' && buf[2] == 'E' && buf[3] == 0x00) {
    strcpy(pre, "LME");		/* LegLess */

  } else if (buf[0] == 'P' && buf[1] == 'S' && buf[2] == 'A' && buf[3] == 0x00) {
    strcpy(pre, "PSA");		/* PSA */

  } else if ((buf[0] == 'S' && buf[1] == 'y' && buf[2] == 'n' && buf[3] == 't'
	      && buf[4] == 'h' && buf[6] == '.' && buf[8] == 0x00)
	     && (buf[5] > '1' && buf[5] < '4')) {
    strcpy(pre, "SYN");		/* Synthesis */

  } else if (buf[0xbc6] == '.' && buf[0xbc7] == 'F' && buf[0xbc8] == 'N'
	     && buf[0xbc9] == 'L') {
    strcpy(pre, "DM2");		/* Delta 2.0 */

  } else if (buf[0] == 'R' && buf[1] == 'J' && buf[2] == 'P') {

    if (buf[4] == 'S' && buf[5] == 'M' && buf[6] == 'O' && buf[7] == 'D') {
      strcpy(pre, "RJP");	/* Vectordean (Richard Joseph Player) */
    } else {
      strcpy(pre, "");		/* but don't play .ins files */
    }
  } else if (buf[0] == 'F' && buf[1] == 'O' && buf[2] == 'R' && buf[3] == 'M') {
    if (buf[8] == 'S' && buf[9] == 'M' && buf[10] == 'U' && buf[11] == 'S') {
      strcpy(pre, "SMUS");	/* Sonix */
    }
    // } else if (buf[0x00] == 0x00 && buf[0x01] == 0xfe &&
    //            buf[0x30] == 0x00 && buf[0x31] ==0x00 && buf[0x32] ==0x01 && buf[0x33] ==0x40 &&
    //          realfilesize > 332 ){
    //         }
    //         strcpy (pre, "SMUS");              /* Tiny Sonix*/

  } else if (tronictest(buf, bufsize)) {
    strcpy(pre, "TRONIC");	/* Tronic */

    /* generic ID Check at offset 0x00 */
  } else if (chk_id_offset(buf, bufsize, offset_0000_patterns, 0x00, pre)) {

    /*magic ids of some modpackers */
  } else if (buf[0x438] == 'P' && buf[0x439] == 'W' && buf[0x43a] == 'R'
	     && buf[0x43b] == 0x2e) {
    strcpy(pre, "PPK");		/*Polkapacker */

  } else if (buf[0x100] == 'S' && buf[0x101] == 'K' && buf[0x102] == 'Y'
	     && buf[0x103] == 'T') {
    strcpy(pre, "SKT");		/*Skytpacker */

  } else
      if ((buf[0x5b8] == 'I' && buf[0x5b9] == 'T' && buf[0x5ba] == '1'
	   && buf[0x5bb] == '0') || (buf[0x5b8] == 'M' && buf[0x5b9] == 'T'
				     && buf[0x5ba] == 'N'
				     && buf[0x5bb] == 0x00)) {
    strcpy(pre, "ICE");		/*Ice/Soundtracker 2.6 */

  } else if (buf[0x3b8] == 'K' && buf[0x3b9] == 'R' && buf[0x3ba] == 'I'
	     && buf[0x3bb] == 'S') {
    strcpy(pre, "KRIS");	/*Kristracker */

/* Custom file check */
  } else if (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x03
	     && buf[3] == 0xf3) {
     /*CUSTOM*/ i = (buf[0x0b] * 4) + 0x1c;	/* beginning of first chunk */

    if (i < bufsize - 0x42) {

      t = 0;
      /* unfort. we can't always assume: moveq #-1,d0 rts before "delirium" */
      /* search 0x40 bytes from here, (enough?) */
      while ((buf[i + t + 0] != 'D' && buf[i + t + 1] != 'E'
	      && buf[i + t + 2] != 'L' && buf[i + t + 3] != 'I')
	     && (t < 0x40)) {
	t++;
      }

      if (t < 0x40) {
	/* longword after Delirium is rel. offset from first chunk 
	   where "hopefully" the delitags are */
	int s = (buf[i + t + 10] * 256) + buf[i + t + 11] + i;	/* 64K */
	if (s < bufsize - 0x33) {
	  for (i = 0; i < 0x30; i = i + 4) {
	    if (buf[i + s + 0] == 0x80 && buf[i + s + 1] == 0x00 &&
		buf[i + s + 2] == 0x44 && buf[i + s + 3] == 0x55) {
	      strcpy(pre, "CUST");	/* CUSTOM */
	      break;
	    }
	  }
	}
      }
    }

  } else if (buf[12] == 0x00) {
    int s = (buf[12] * 256 + buf[13] + 1) * 14;
    if (s < (bufsize - 91)) {
      if (buf[80 + s] == 'p' && buf[81 + s] == 'a' && buf[82 + s] == 't'
	  && buf[83 + s] == 't' && buf[87 + s] == 32 && buf[88 + s] == 'p'
	  && buf[89 + s] == 'a' && buf[90 + s] == 't' && buf[91 + s] == 't') {
	strcpy(pre, "PUMA");	/* Pumatracker */
      }
    }
  }
}


/* We are currently stupid and check only for a few magic IDs at the offsets
 * chk_id_offset returns 1 on success and sets the right prefix/extension
 * in pre
 * TODO: more and less easy check for the rest of the 52 trackerclones
 */
static int chk_id_offset(unsigned char *buf, int bufsize,
			 const char *patterns[], int offset, char *pre)
{
  int i;
  for (i = 0; patterns[i]; i = i + 2) {
    if (patterntest(buf, patterns[i], offset, strlen(patterns[i]), bufsize)) {
      /* match found */
      strcpy(pre, patterns[i + 1]);
      return 1;
    }
  }
  return 0;
}
