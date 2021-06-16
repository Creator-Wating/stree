static char *WhatString[]= {
  "@(#)pstree $Revision: 2.33 $ by Fred Hucht (C) 1993-2007",
  "@(#)EMail: fred AT thp.Uni-Duisburg.de",
  "$Id: pstree.c,v 2.33 2009-11-10 22:12:39+01 fred Exp $"
};

#define MAXLINE 8192

/* TRU64 contributed by Frank Parkin <fparki AT acxiom.co.uk>
 */
#  ifdef __linux
#    define USE_GetProcessesDirect
#    include <glob.h>
#    include <sys/stat.h>
#  endif
#  define UID2USER
#  define HAS_PGID
#  define PSCMD 	"ps -eo uid,pid,ppid,pgid,args"
#  define PSFORMAT 	"%ld %ld %ld %ld %[^\n]"
#  define PSVARS	&P[i].uid, &P[i].pid, &P[i].ppid, &P[i].pgid, P[i].cmd
#  define PSVARSN	5

#include <stdio.h>
#include <stdlib.h>
#include <string.h>		/* For str...() */
#include <unistd.h>		/* For getopt() */
#include <pwd.h>		/* For getpwnam() */

#include <sys/ioctl.h>		/* For TIOCGSIZE/TIOCGWINSZ */
/* #include <termios.h> */

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

struct TreeChars {
  char *s2, 		/* SS String between header and pid */
    *p, 		/* PP dito, when parent of printed childs */
    *pgl,		/* G  Process group leader */
    *npgl,		/* N  No process group leader */
    *barc, 		/* C  bar for line with child */
    *bar, 		/* B  bar for line without child */
    *barl,		/* L  bar for last child */
    *sg,		/*    Start graphics (alt char set) */
    *eg,		/*    End graphics (alt char set) */
    *init;		/*    Init string sent at the beginning */
};

/* Example:
 * |-+- 01111 ...        CPPN 01111 ...
 * | \-+=   01112 ...    B LPPG 01112 ...
 * |   |--= 01113 ...    B   CSSG 01113 ...
 * |   \--= 01114 ...    B   LSSG 01114 ...
 * \--- 01115 ...        LSSN 01115 ...
 */

enum { G_ASCII = 0, G_PC850 = 1, G_VT100 = 2, G_UTF8 = 3, G_LAST };

/* VT sequences contributed by Randall Hopper <rhh AT ct.picker.com> */
/* UTF8 sequences contributed by Mark-Andre Hopf <mhopf AT mark13.org> */
static struct TreeChars TreeChars[] = {
  /* SS          PP          G       N       C       B       L      sg      eg      init */
  { "--",       "-+",       "=",    "-",    "|",    "|",    "\\",   "",     "",     ""             }, /*Ascii*/
  { "\304\304", "\304\302", "\372", "\304", "\303", "\263", "\300", "",     "",     ""             }, /*Pc850*/
  { "qq",       "qw",       "`",    "q",    "t",    "x",    "m",    "\016", "\017", "\033(B\033)0" }, /*Vt100*/
  { "\342\224\200\342\224\200",
    /**/        "\342\224\200\342\224\254",
    /**/                    "=",
    /**/                            "\342\224\200",
    /**/                                    "\342\224\234",
    /**/                                            "\342\224\202",
    /**/                                                    "\342\224\224",
    /**/                                                            "",     "",     ""             }  /*UTF8*/
}, *C;

int MyPid, NProc, Columns, RootPid;
short showall = TRUE, soption = FALSE, Uoption = FALSE, Hoption = FALSE, noption = FALSE, roption = FALSE;
char *name = "", *str = NULL, *Progname;
long ipid = -1;
char *input = NULL;

int atLdepth=0;    /* LOPTION - track how deep in the print chain we are */
int maxLdepth=100; /* LOPTION - will be changed by -l n option */

#ifdef DEBUG
int debug = FALSE;
#endif

struct Proc {
  long uid, pid, ppid, pgid;
  char name[32], cmd[MAXLINE];
  int  print;
  int  selected;
  int  hidden;
  long parent, child, sister;
  unsigned long thcount;
} *P;

#ifdef UID2USER
void uid2user(uid_t uid, char *name, int len) {
#define NUMUN 128
  static struct un_ {
    uid_t uid;
    char name[32];
  } un[NUMUN];
  static short n = 0;
  short i;
  char uid_name[32];
  char *found;
#ifdef DEBUG
  if (name == NULL) {
    for (i = 0; i < n; i++)
      fprintf(stderr, "uid = %3d, name = %s\n", un[i].uid, un[i].name);
    return;
  }
#endif
  for (i = n - 1; i >= 0 && un[i].uid != uid; i--);
  if (i >= 0) { /* found locally */
    found = un[i].name;
  } else {
    struct passwd *pw = getpwuid(uid);
    if (pw) {
      found = pw->pw_name;
    } else {
      /* fix by Stan Sieler & Philippe Torche */
      snprintf(uid_name, sizeof(uid_name), "#%d", uid);
      found = uid_name;
    }
    if (n < NUMUN) {
      un[n].uid = uid;
      strncpy(un[n].name, found, 9);
      un[n].name[8] = '\0';
      n++;
    }
  }
  strncpy(name, found, len);
  name[len-1] = '\0';
}
#endif

#ifdef __linux
int GetProcessesDirect(void) {
  glob_t globbuf;
  unsigned int i, j;
  
  glob("/proc/[0-9]*", GLOB_NOSORT, NULL, &globbuf);
  
  P = calloc(globbuf.gl_pathc, sizeof(struct Proc));
  if (P == NULL) {
    fprintf(stderr, "Problems with malloc.\n");
    exit(1);
  }
  
  for (i = j = 0; i < globbuf.gl_pathc; i++) {
    char name[32];
    int c;
    FILE *tn;
    struct stat stat;
    int k = 0;
    
    snprintf(name, sizeof(name), "%s%s",
	     globbuf.gl_pathv[globbuf.gl_pathc - i - 1], "/stat");
    tn = fopen(name, "r");
    if (tn == NULL) continue; /* process vanished since glob() */
    fscanf(tn, "%ld %s %*c %ld %ld",
	   &P[j].pid, P[j].cmd, &P[j].ppid, &P[j].pgid);
    fstat(fileno(tn), &stat);
    P[j].uid = stat.st_uid;
    fclose(tn);
    P[j].thcount = 1;
    
    snprintf(name, sizeof(name), "%s%s",
	     globbuf.gl_pathv[globbuf.gl_pathc - i - 1], "/cmdline");
    tn = fopen(name, "r");
    if (tn == NULL) continue;
    while (k < MAXLINE - 1 && EOF != (c = fgetc(tn))) {
      P[j].cmd[k++] = c == '\0' ? ' ' : c;
    }
    if (k > 0) P[j].cmd[k] = '\0';
    fclose(tn);
    
    uid2user(P[j].uid, P[j].name, sizeof(P[j].name));
    
#ifdef DEBUG
    if (debug) fprintf(stderr,
		       "uid=%5ld, name=%8s, pid=%5ld, ppid=%5ld, pgid=%5ld, thcount=%ld, cmd='%s'\n",
		       P[j].uid, P[j].name, P[j].pid, P[j].ppid, P[j].pgid, P[j].thcount, P[j].cmd);
#endif
    P[j].parent = P[j].child = P[j].sister = -1;
    P[j].print  = FALSE;
    j++;
  }
  globfree(&globbuf);
  return j;
}
#endif /* __linux */

int GetProcesses(void) {
  FILE *tn;
  int i = 0;
  char line[MAXLINE], command[] = PSCMD;
  
  /* file read code contributed by Paul Kern <pkern AT utcc.utoronto.ca> */
  if (input != NULL) {
    if (strcmp(input, "-") == 0)
      tn = stdin;
    else if (NULL == (tn = fopen(input,"r"))) {
      perror(input);
      exit(1);
    }
  } else {
#ifdef DEBUG
    if (debug) fprintf(stderr, "calling '%s'\n", command);
#endif
    if (NULL == (tn = (FILE*)popen(command,"r"))) {
      perror("Problems with pipe");
      exit(1);
    }
  }
#ifdef DEBUG
  if (debug) fprintf(stderr, "popen:errno = %d\n", errno);
#endif
  
  if (NULL == fgets(line, MAXLINE, tn)) { /* Throw away header line */
    fprintf(stderr, "No input.\n");
    exit(1);
  }
  
#ifdef DEBUG
  if (debug) fputs(line, stderr);
#endif
  
  P = malloc(sizeof(struct Proc));
  if (P == NULL) {
    fprintf(stderr, "Problems with malloc.\n");
    exit(1);
  }
  
  while (NULL != fgets(line, MAXLINE, tn)) {
    int len, num;
    len = strlen(line);
#ifdef DEBUG
    if (debug) {
      fprintf(stderr, "len=%3d ", len);
      fputs(line, stderr);
    }
#endif
    
    if (len == MAXLINE - 1) { /* line too long, drop remaining stuff */
      char tmp[MAXLINE];
      while (MAXLINE - 1 == strlen(fgets(tmp, MAXLINE, tn)));
    }      
    
    P = realloc(P, (i+1) * sizeof(struct Proc));
    if (P == NULL) {
      fprintf(stderr, "Problems with realloc.\n");
      exit(1);
    }
    
    memset(&P[i], 0, sizeof(*P));
    
#ifdef solaris1x
    { /* SunOS allows columns to run together.  With the -j option, the CPU
       * time used can run into the numeric user id, so make sure there is
       * space between these two columns.  Also, the order of the desired
       * items is different. (L. Mark Larsen <mlarsen AT ptdcs2.intel.com>)
       */
      char buf1[45], buf2[MAXLINE];
      buf1[44] = '\0';
      sscanf(line, "%44c%[^\n]", buf1, buf2);
      snprintf(line, sizeof(line), "%s %s", buf1, buf2);
    }
#endif
    
    num = sscanf(line, PSFORMAT, PSVARS);
    
    if (num != PSVARSN) {
#ifdef DEBUG
      if (debug) fprintf(stderr, "dropped line, num=%d != %d\n", num, PSVARSN);
#endif
      continue;
    }
    
#ifdef UID2USER	/* get username */
    uid2user(P[i].uid, P[i].name, sizeof(P[i].name));
#endif

#ifdef DEBUG
    if (debug) fprintf(stderr,
		      "uid=%5ld, name=%8s, pid=%5ld, ppid=%5ld, pgid=%5ld, thcount=%ld, cmd='%s'\n",
		      P[i].uid, P[i].name, P[i].pid, P[i].ppid, P[i].pgid, P[i].thcount, P[i].cmd);
#endif
    P[i].parent = P[i].child = P[i].sister = -1;
    P[i].print  = FALSE;
    i++;
  }
  if (input != NULL)
    fclose(tn);
  else
    pclose(tn);
  return i;
}

int GetRootPid(void) {
  int me;
  for (me = 0; me < NProc; me++) {
    if (P[me].pid == 1) return P[me].pid;
  }
  /* PID == 1 not found, so we'll take process with PPID == 0
   * Fix for TRU64 TruCluster with uniq PIDs
   * reported by Frank Parkin <fparki AT acxiom.co.uk>
   * re-reported by Eric van Doorn <Eric.van.Doorn AT isc.politie.nl>,
   * because fix was not published by me :-/ */
  for (me = 0; me < NProc; me++) {
    if (P[me].ppid == 0) return P[me].pid;
  }
  /* OK, still nothing found. Maybe it is FreeBSD and won't show foreign
   * processes. So we also accept PPID == 1 */
  for (me = 0; me < NProc; me++) {
    if (P[me].ppid == 1) return P[me].pid;
  }
  /* Still nothing. Maybe it is something like Solaris Zone. We'll take
   * the process with PID == PPID */
  for (me = 0; me < NProc; me++) {
    if (P[me].pid == P[me].ppid) return P[me].pid;
  }
  /* Should not happen */
  fprintf(stderr,
	  "%s: No process found with PID == 1 || PPID == 0 || PPID == 1\n"
	  "          || PID == PPID, contact author.\n",
	  Progname);
  exit(1);
}

#ifdef ZOMBIES_HAVE_PID_0
void FixZombies(void) {
  int me, num = 0;
  for (me = 0; me < NProc; me++) {
    if (P[me].pid == 0) num++;
  }
  if (num > 1) for (me = 0; me < NProc; me++) {
    if (P[me].pid == 0 && P[me].ppid != 0 && P[me].ppid != -1) {
      P[me].pid = -1;
#ifdef DEBUG
      if (debug) fprintf(stderr,
			 "fixed zombie %s with ppid %d\n",
			 P[me].cmd, P[me].ppid);
#endif
    }
  }
}
#endif

int get_pid_index(long pid) {
  int me;
  for (me = NProc - 1;me >= 0 && P[me].pid != pid; me--); /* Search process */
  return me;
}

#define EXIST(idx) ((idx) != -1)

void MakeTree(void) {
  /* Build the process hierarchy. Every process marks itself as first child
   * of it's parent or as sister of first child of it's parent */
  int me;  
  for (me = 0; me < NProc; me++) {
    int parent;
    parent = get_pid_index(P[me].ppid);
    if (parent != me && parent != -1) { /* valid process, not me */
      P[me].parent = parent;
      if (P[parent].child == -1) /* first child */
	P[parent].child = me;
      else {
	int sister;
	for (sister = P[parent].child; EXIST(P[sister].sister); sister = P[sister].sister);
	P[sister].sister = me;
      }
    }
  }
}

void MarkChildren(int me) {
  int child;
  P[me].print = TRUE;
  for (child = P[me].child; EXIST(child); child = P[child].sister)
    MarkChildren(child);
}

void MarkProcs(void) {
  int me;
  for (me = 0; me < NProc; me++) {
    if (showall) {
      P[me].print = TRUE;
    } else {
      int parent;
      if (0 == strcmp(P[me].name, name)		/* for -u */
	 || (Uoption &&
	     0 != strcmp(P[me].name, "root"))	/* for -U */
	 || P[me].pid == ipid			/* for -p */
	 || (soption
	     && NULL != strstr(P[me].cmd, str)
	     && P[me].pid != MyPid)		/* for -s */
	 ) {
	P[me].selected = TRUE;
	/* Mark parents */
	for (parent = P[me].parent; EXIST(parent); parent = P[parent].parent) {
	  P[parent].print = TRUE;
	  if (Hoption) P[parent].hidden = TRUE;
	}
	/* Mark children */
	MarkChildren(me);
      }
    }
#if 0 /* experimental thread compression */
    {
      int parent = P[me].parent;
      int ancestor; /* oldest parent with same cmd */
      if (0 == strcmp(P[me].cmd, P[parent].cmd)) {
	P[me].print = FALSE;
	for (parent = P[me].parent;
	     EXIST(parent) && (0 == strcmp(P[me].cmd, P[parent].cmd));
	     parent = P[parent].parent) {
	  ancestor = parent;
	}
	fprintf(stderr, "%d: %d\n",
		P[me].pid,
		P[ancestor].pid);
	P[ancestor].thcount++;
      }
    }
#endif
  }
}

void DropProcs(void) {
  int me;
  for (me = 0; me < NProc; me++) if (P[me].print) {
    int child, sister;
    /* Drop children that won't print */
    for (child = P[me].child;
	 EXIST(child) && !P[child].print; child = P[child].sister);
    P[me].child = child;
    /* Drop sisters that won't print */
    for (sister = P[me].sister;
	 EXIST(sister) && !P[sister].print; sister = P[sister].sister);
    P[me].sister = sister;
    if (roption && P[me].pid == 1)
      P[me].hidden = TRUE;
  }
}

void PrintTree(int idx, const char *head) {
  char nhead[MAXLINE], out[4 * MAXLINE], thread[16] = {'\0'};
  int child;
  
  if (head[0] == '\0' && !P[idx].print) return;
  
  if (P[idx].thcount > 1) snprintf(thread, sizeof(thread), "[%ld]", P[idx].thcount);
 
  if(atLdepth == maxLdepth) return;    /* LOPTION */
  ++atLdepth;                          /* LOPTION */
 
  
  snprintf(out, sizeof(out),
	   "%s%s%s%s%s%s %5ld %s %s%s" /*" (ch=%d, si=%d, pr=%d)"*/,
	   C->sg,
	   head,
	   head[0] == '\0' ? "" : EXIST(P[idx].sister) ? C->barc : C->barl,
	   EXIST(P[idx].child)       ? C->p   : C->s2,
	   P[idx].pid == P[idx].pgid ? C->pgl : C->npgl,
	   C->eg,
	   P[idx].pid,
	   !noption ? P[idx].name : "",
	   thread,
	   P[idx].cmd
	   /*,P[idx].child,P[idx].sister,P[idx].print*/);
  
  out[Columns-1] = '\0';
  if (P[idx].hidden && !P[idx].selected) {
    snprintf(nhead, sizeof(nhead), "%s", head);
  } else {
    puts(out);
  
    /* Process children */
    snprintf(nhead, sizeof(nhead), "%s%s ", head,
        head[0] == '\0' ? "" : EXIST(P[idx].sister) ? C->bar : " ");
  }
  
  for (child = P[idx].child; EXIST(child); child = P[child].sister)
    PrintTree(child, nhead);

  --atLdepth;                          /* LOPTION */

}

void Usage(void) {
  fprintf(stderr,
	  "%s\n"
	  "%s\n\n"
	  "Usage: %s "
#ifdef DEBUG
	  "[-d] "
#endif
	  "[-f file] [-g n] [-u user] [-U] [-s string] [-H] [-p pid] [-w] [pid ...]\n"
	  /*"   -a        align output\n"*/
#ifdef DEBUG
	  "   -d        print debugging info to stderr\n"
#endif
	  "   -f file   read input from <file> (- is stdin) instead of running\n"
	  "             \"%s\"\n"
	  "   -g n      use graphics chars for tree. n=1: IBM-850, n=2: VT100, n=3: UTF-8\n"
	  "   -l n      print tree to n level deep\n"
	  "   -u user   show only branches containing processes of <user>\n"
	  "   -U        don't show branches containing only root processes\n"
	  "   -s string show only branches containing process with <string> in commandline\n"
	  "   -p pid    show only branches containing process <pid>\n"
	  "   -H        show only selected subtrees (hide parents)\n"
	  "   -n        hide usernames\n"
	  "   -r        hide root process (pid 1)\n"
	  "   -w        wide output, not truncated to window width\n"
	  "   pid ...   process ids to start from, default is 1 (init)\n"
	  "             use 0 to also show kernel processes\n"
	  , WhatString[0] + 4, WhatString[1] + 4, Progname, PSCMD);
#ifdef HAS_PGID
  fprintf(stderr, "\n%sProcess group leaders are marked with '%s%s%s'.\n",
	  C->init, C->sg, C->pgl, C->eg);
#endif
  exit(1);
}

int main(int argc, char **argv) {
  extern int optind;
  extern char *optarg;
  int ch;
  long pid;
  int graph = G_UTF8, wide = FALSE;
  
  C = &TreeChars[graph];
  
  Progname = strrchr(argv[0],'/');
  Progname = (NULL == Progname) ? argv[0] : Progname + 1;
  
  while ((ch = getopt(argc, argv, "df:g:hl:p:s:u:Uw?Hnr")) != EOF)
    switch(ch) {
      /*case 'a':
	align   = TRUE;
	break;*/
#ifdef DEBUG
    case 'd':
      debug   = TRUE;
      break;
#endif
    case 'f':
      input   = optarg;
      break;
    case 'g':
      graph   = atoi(optarg);
      if (graph < 0 || graph >= G_LAST) {
	fprintf(stderr, "%s: Invalid graph parameter.\n",
		Progname);
	exit(1);
      }
      C = &TreeChars[graph];
      break;
    case 'l':                                 /* LOPTION */
      maxLdepth = atoi(optarg);               /* LOPTION */
      if(maxLdepth < 1) maxLdepth = 1;        /* LOPTION */
      break;                                  /* LOPTION */
    case 'H':
      Hoption = TRUE;
      break;
    case 'n':
      noption = TRUE;
      break;
    case 'r':
      roption = TRUE;
      break;
    case 'p':
      showall = FALSE;
      ipid    = atoi(optarg);
      break;
    case 's':
      showall = FALSE;
      soption = TRUE;
      str     = optarg;
      break;
    case 'u':
      showall = FALSE;
      name    = optarg;
      if (
#ifdef solaris2x
	 (int)
#endif
	 NULL == getpwnam(name)) {
	fprintf(stderr, "%s: User '%s' does not exist.\n",
		Progname, name);
	exit(1);
      }
      break;
    case 'U':
      showall = FALSE;
      Uoption = TRUE;
      break;
    case 'w':
      wide    = TRUE;
      break;
    case 'h':
    case '?':
    default :
      Usage();
      break;
    }
  
#ifdef USE_GetProcessesDirect
  NProc = input == NULL ? GetProcessesDirect() : GetProcesses();
#else
  NProc = GetProcesses();
#endif
  
#ifdef ZOMBIES_HAVE_PID_0
  FixZombies();
#endif
  
  if (NProc == 0) {
    fprintf(stderr, "%s: No processes read.\n", Progname);
    exit(1);
  }

#ifdef DEBUG
  if (debug) fprintf(stderr, "NProc = %d processes found.\n", NProc);
#endif
  
  RootPid = GetRootPid();

#ifdef DEBUG
  if (debug) fprintf(stderr, "RootPid = %d.\n", RootPid);
#endif

#if defined(UID2USER) && defined(DEBUG)
  if (debug) uid2user(0,NULL,0);
#endif
  MyPid = getpid();
  
  if (wide)
    Columns = MAXLINE - 1;
  else {
#if defined (HAS_TERMDEF)
    Columns = atoi((char*)termdef(fileno(stdout),'c'));
#elif defined(TIOCGWINSZ)
    struct winsize winsize;
    ioctl(fileno(stdout), TIOCGWINSZ, &winsize);
    Columns = winsize.ws_col;
#elif defined(TIOCGSIZE)
    struct ttysize ttysize;
    ioctl(fileno(stdout), TIOCGSIZE, &ttysize);
    Columns = ttysize.ts_cols;
#else
    char *env = getenv("COLUMNS");
    Columns = env ? atoi(env) : 80;
#endif
  }
  if (Columns == 0) Columns = MAXLINE - 1;
  
  printf("%s", C->init);
  
  Columns += strlen(C->sg) + strlen(C->eg); /* Don't count hidden chars */

  if (Columns >= MAXLINE) Columns = MAXLINE - 1;
  
#ifdef DEBUG
  if (debug) fprintf(stderr, "Columns = %d\n", Columns);
#endif
  
  MakeTree();
  MarkProcs();
  DropProcs();
  
  if (argc == optind) { /* No pids */
    PrintTree(get_pid_index(RootPid), "");
  } else while (optind < argc) {
    int idx;
    pid = (long)atoi(argv[optind]);
    idx = get_pid_index(pid);
    if (idx > -1) PrintTree(idx, "");
    optind++;
  }
  free(P);
  return 0;
}
