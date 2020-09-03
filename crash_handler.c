/*
 * crash_handler.c - a program to record crashes into crash report files
 *
 * Copyright 2011,2012 Sony Network Entertainment
 *
 * Author: Tim Bird <tim.bird (at) am.sony.com>
 *
 * Lots of stuff copied from Android debuggerd:
 * system/debuggerd/debuggerd.c
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/
/************************
 *  Install this program as the crash_handler for a Linux system with:
 *  $ cp crash_handler /tmp
 *  $ /tmp/crash_handler install
 *  # 'tmp' can be any directory
 *  test with:
 *  $ /tmp/fault-test
 *  $ cat /tmp/crash_reports/crash_report_0x
 *  # where x is the latest crash_report
 ************************
*********************************************/

/* #define _GNU_SOURCE */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <stdarg.h>
#include <signal.h>
#include <asm/ptrace.h>
#include <elf.h>
#include <sys/wait.h>

#include "utility.h"
#include "crash_handler.h"

#define VERSION	0
#define REVISION 6

/****************************************
 * compile-time configurable items
 ****************************************/
#define MAX_CRASH_REPORTS	10
#define CRASH_REPORT_DIR	"/tmp/crash_reports"
#define CRASH_REPORT_FILENAME	"crash_report"

#define DO_CRASH_JOURNAL	1
#define CRASH_JOURNAL_FILENAME	"/tmp/crash_journal"

/* set to 1 to save a full core file for each crash report */
#define DO_CORE_FILE 	0

/* select which unwinder(s) to use for backtrace */
#define USE_TABLE_UNWINDER	1
#define USE_GUESS_UNWINDER	1
#define USE_MCTERNAN_UNWINDER	0	/* not integrated yet */

/****************************************/

#if DO_CRASH_JOURNAL
extern void record_crash_to_journal(char *filename, int pid, char *name);
#else
#define record_crash_to_journal(a,b,c)
#endif

/* IS_ELF is defined in android ndk sys/exec_elf.h, but not in elf.h */
#define IS_ELF(ehdr) ((ehdr).e_ident[EI_MAG0] == ELFMAG0 && \
		     (ehdr).e_ident[EI_MAG1] == ELFMAG1 && \
		     (ehdr).e_ident[EI_MAG2] == ELFMAG2 && \
		     (ehdr).e_ident[EI_MAG3] == ELFMAG3)

#define BUF_SIZE 512
#define ROOT_UID 0
#define ROOT_GID 0

int report_fd = -1;
int ts_num = -1;
mapinfo stack_map;

void klog_fmt(const char *fmt, ...)
{
	int fd;
	pid_t pid;
	char format[BUF_SIZE];
	char buf[BUF_SIZE];
	int len;
	
    	va_list ap;
    	va_start(ap, fmt);

	pid = getpid();
	fd = open("/dev/kmsg", O_WRONLY);
	if(fd<0) {
		return;
	}
	
	sprintf(format, "<21> [%d] ", pid);
	strncat(format, fmt, BUF_SIZE);
	format[BUF_SIZE-1] = 0;

	vsnprintf(buf, sizeof(buf), format, ap);
	buf[BUF_SIZE-1] = 0;

	len = strlen(buf);
	write(fd, buf, len);
	close(fd);
}


/* Log information into the crash_report */
void report_out(int rfd, const char *fmt, ...)
{
    char buf[BUF_SIZE];
    
    va_list ap;
    va_start(ap, fmt);

    if( rfd >= 0 ) {
	    int len;
	    vsnprintf(buf, sizeof(buf), fmt, ap);
	    len = strlen(buf);
	    write(rfd, buf, len);
    } 
}

#define typecheck(x,y) {    \
    typeof(x) __dummy1;     \
    typeof(y) __dummy2;     \
    (void)(&__dummy1 == &__dummy2); }

/*
 * find_and_open_crash_report - find an available crash report slot, if any,
 * of the form 'crash_report_XX where XX is 00 to MAX_CRASH_REPORTS-1,
 * inclusive. If no file is available, we reuse the least-recently-modified
 * file.
 */
static int find_and_open_crash_report(void)
{
    time_t mtime = ULONG_MAX;
    struct stat sb;
    char path[128];
    int fd, i, oldest = 0;

    /*
     * XXX: Android stat.st_mtime may not be time_t.
     * This check will generate a warning in that case.
     */
    typecheck(mtime, sb.st_mtime);

    /* FIXTHIS - should probably create leading directories also */
    mkdir(CRASH_REPORT_DIR, 0755);

    /*
     * In a single wolf-like pass, find an available slot and, in case none
     * exist, find and record the least-recently-modified file.
     */
    for (i = 0; i < MAX_CRASH_REPORTS; i++) {
        snprintf(path, sizeof(path),
		CRASH_REPORT_DIR"/"CRASH_REPORT_FILENAME"_%02d", i);
	ts_num = i;

        if (!stat(path, &sb)) {
            if (sb.st_mtime < mtime) {
                oldest = i;
                mtime = sb.st_mtime;
            }
            continue;
        }
        if (errno != ENOENT)
            continue;

        fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd < 0)
            continue;	/* raced ? */

        fchown(fd, ROOT_UID, ROOT_GID);
        return fd;
    }

    /* we didn't find an available file, so we clobber the oldest one */
    snprintf(path, sizeof(path),
		CRASH_REPORT_DIR"/"CRASH_REPORT_FILENAME"_%02d", i);
    ts_num = oldest;

    fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    fchown(fd, ROOT_UID, ROOT_GID);

    return fd;
}


/* Main entry point to get the backtrace from the crashing process */
extern int table_unwind_backtrace_with_ptrace(pid_t pid, mapinfo *map,
                                        unsigned int sp_list[],
                                        int *frame0_pc_sane);

extern int guess_unwind_backtrace_with_ptrace(pid_t pid, mapinfo *map,
					unsigned int sp_list[],
                                        int *frame0_pc_sane);

void dump_task_info(pid_t pid, unsigned sig, unsigned uid, unsigned gid)
{
    char path[256];
    char buffer[1024];
    char cmdline[1024];
    char name[20];
    char *s;
    int fd;
    int count;
    
    strcpy(cmdline, "UNKNOWN");
    sprintf(path, "/proc/%d/cmdline", pid);
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        count = read(fd, buffer, 1024);
        DLOG("count=%d\n", count);
	strncpy(cmdline, buffer, 1024);
	cmdline[1023] = 0;
        DLOG("cmdline=%s\n", cmdline);
        close(fd);
    } else {
        DLOG("problem opening %s\n", path);
    }
    sprintf(path, "/proc/%d/status", pid);
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        count = read(fd, buffer, 1024);
        DLOG("count=%d\n", count);
	/* first line is: Name:\t<name>\n */
	s = strchr(buffer, '\n');
	*s = 0;
	strcpy(name, buffer+6);
        DLOG("name=%s\n", name);
        close(fd);
    } else {
        DLOG("problem opening %s\n", path);
    }
    
    LOG("[task info]\n");
    LOG("pid: %u, uid: %u, gid: %u \n", pid, uid, gid);
    LOG("cmdline: %s\n", cmdline);
    LOG("name: %s\n", name);
    LOG("signal: %u\n\n", sig);

    record_crash_to_journal(CRASH_JOURNAL_FILENAME, pid, cmdline);
    
}

void dump_word(int pid)
{
    unsigned long data;

    if(ptrace(PTRACE_PEEKTEXT, pid, 0, &data)) {
        LOG("cannot get word: %s\n", strerror(errno));
        return;
    }
    LOG("word at 0 is: %ul\n", data);
}

// 6f000000-6f01e000 rwxp 00000000 00:0c 16389419   /system/lib/libcomposer.so
// 012345678901234567890123456789012345678901234567890123456789
// 0         1         2         3         4         5

/*
 * parse a memory map line
 * Note: only executable maps are returned.  Other maps (data, stack, etc.)
 * are ignored
 */ 
mapinfo *parse_maps_line(char *line)
{
    mapinfo *mi;
    int len = strlen(line);

    if(len < 1) return 0;
    /* cut trailing \n */
    line[--len] = 0;
    
    if(len < 50) return 0;

    /* capture size of stack */
    if (strcmp(line + 49, "[stack]")==0) {
	stack_map.start = strtoul(line, 0, 16);
	stack_map.end = strtoul(line+9, 0, 16);
        strcpy(stack_map.name, line + 49);
	return 0;
    }

    /* ignore non-executable segments */
    if(line[20] != 'x') return 0;

    mi = malloc(sizeof(mapinfo) + (len - 47));
    if(mi == 0) return 0;
    
    mi->start = strtoul(line, 0, 16);
    mi->end = strtoul(line + 9, 0, 16);
    /* To be filled in by parse_exidx_info if the mapped section starts with 
     * elf_header
     */
    mi->exidx_start = mi->exidx_end = 0;
    mi->next = 0;
    strcpy(mi->name, line + 49);

    return mi;
}

void free_mapinfo_list(mapinfo *milist)
{
    while(milist) {
        mapinfo *next = milist->next;
        free(milist);
        milist = next;
    }
}

mapinfo *get_mapinfo_list(pid_t pid)
{
    char data[1024];
    FILE *fp;
    mapinfo *milist = 0;

    sprintf(data, "/proc/%d/maps", pid);
    fp = fopen(data, "r");
    if(fp) {
        while(fgets(data, 1024, fp)) {
	    LOG(" %s", data);
            mapinfo *mi = parse_maps_line(data);
            if(mi) {
                mi->next = milist;
                milist = mi;
            }
        }
        fclose(fp);
    }

    return milist;
}

void dump_registers(int pid) 
{
    struct pt_regs r;
    //pt_regs r;

    LOG("[registers]\n");
    if(ptrace(PTRACE_GETREGS, pid, 0, &r)) {
        LOG("cannot get registers: %d (%s)\n", errno, strerror(errno));
        return;
    }
    
    LOG(" r0 %08x  r1 %08x  r2 %08x  r3 %08x\n",
         r.ARM_r0, r.ARM_r1, r.ARM_r2, r.ARM_r3);
    LOG(" r4 %08x  r5 %08x  r6 %08x  r7 %08x\n",
         r.ARM_r4, r.ARM_r5, r.ARM_r6, r.ARM_r7);
    LOG(" r8 %08x  r9 %08x  10 %08x  fp %08x\n",
         r.ARM_r8, r.ARM_r9, r.ARM_r10, r.ARM_fp);
    LOG(" ip %08x  sp %08x  lr %08x  pc %08x  cpsr %08x\n",
         r.ARM_ip, r.ARM_sp, r.ARM_lr, r.ARM_pc, r.ARM_cpsr);  
    LOG("\n");
}

const char *get_signame(int sig)
{
    switch(sig) {
    case SIGILL:     return "SIGILL";
    case SIGABRT:    return "SIGABRT";
    case SIGBUS:     return "SIGBUS";
    case SIGFPE:     return "SIGFPE";
    case SIGSEGV:    return "SIGSEGV";
    case SIGSTKFLT:  return "SIGSTKFLT";
    default:         return "?";
    }
}

void dump_fault_addr(int pid, int sig)
{
    siginfo_t si;
    
    LOG("[exception info]\n");
    memset(&si, 0, sizeof(si));
    if(ptrace(PTRACE_GETSIGINFO, pid, 0, &si)){
        LOG("cannot get siginfo: %d (%s) \n", errno, strerror(errno));
    } else {
        LOG("signal %d (%s), fault addr %08x\n",
            sig, get_signame(sig), si.si_addr);
    }
    LOG("\n");
}

int dump_pc_code(int pid)
{
    struct pt_regs r;
    //pt_regs r;
    int *start, *end;
    int val;
    int *i;

    LOG("[code around PC]\n");
    if(ptrace(PTRACE_GETREGS, pid, 0, &r)) {
        LOG("cannot get registers: %d (%s)\n", errno, strerror(errno));
        return;
    }
    
    start = (int *)r.ARM_pc-0x10;
    end = (int *)r.ARM_pc+0x10;
    for(i = start; i<end; i++) {
	val = get_remote_word(pid, i);
	LOG("0x%08lx: %08lx", (unsigned long)i, val);
	LOG((unsigned long)i==r.ARM_pc ? " <-- PC\n" : "\n");
    }
}

/* 
 * fill in the exidx_start and _end fills in the mapinfo structures
 * based on ELF headers found in the executable sections
 * */
static void parse_exidx_info(pid_t pid, mapinfo *milist)
{
    mapinfo *mi;
    int i;

    //LOG("Scanning for EXIDX info...\n");
    for (mi = milist; mi != NULL; mi = mi->next) {
        Elf32_Ehdr ehdr;

        memset(&ehdr, 0, sizeof(ehdr));
        /* Read in sizeof(Elf32_Ehdr) worth of data from the beginning of 
         * mapped section.
         */
	//LOG("Scanning at 0x%08lx:\n", mi->start);
        get_remote_struct(pid, (void *) (mi->start), &ehdr, 
                          sizeof(ehdr));
	//LOG("sizeof(ehdr)= %d\n", sizeof(ehdr));
	//LOG("contents of (ehdr):\n");
	/*for(i=0; i<sizeof(ehdr)/4; i+=4) {
		char *s = ((void *)&ehdr)+i;
		LOG(" %x %x %x %x - %c %c %c %c\n",
			*s, *(s+1), *(s+2), *(s+3),
			*s, *(s+1), *(s+2), *(s+3));
	}
	*/

        /* Check if it has the matching magic words */
        if (IS_ELF(ehdr)) {
	    //LOG("Found ELF header in section %s!\n", mi->name);
            Elf32_Phdr phdr;
            Elf32_Phdr *ptr;
            int i;

            ptr = (Elf32_Phdr *) (mi->start + ehdr.e_phoff);
            for (i = 0; i < ehdr.e_phnum; i++) {
                /* Parse the program header */
                get_remote_struct(pid, (void *) ptr+i, &phdr, 
                                  sizeof(Elf32_Phdr));
                /* Found a EXIDX segment? */
                if (phdr.p_type == PT_ARM_EXIDX) {
	    	    //LOG("Found EXIDX segment in section %s!\n", mi->name);
                    mi->exidx_start = mi->start + phdr.p_offset;
                    mi->exidx_end = mi->exidx_start + phdr.p_filesz;
                    break;
                }
            }
        }
    }
}

void dump_stack_and_code(int pid, mapinfo *map, 
                         int unwind_depth, unsigned int sp_list[],
                         int frame0_pc_sane)
{
    unsigned int sp, pc, p, end, data;
    struct pt_regs r;
    int sp_depth;

    if(ptrace(PTRACE_GETREGS, pid, 0, &r)) return;
    sp = r.ARM_sp;
    pc = r.ARM_pc;

    /* Died because calling the weeds - dump
     * the code around the PC in the next frame instead.
     */
    if (frame0_pc_sane == 0) {
        pc = r.ARM_lr;
    }

    LOG("[code]\n");

    end = p = pc & ~3;
    p -= 16;

    /* Dump the code as:
     *  PC         contents
     *  00008d34   fffffcd0 4c0eb530 b0934a0e 1c05447c
     *  00008d44   f7ff18a0 490ced94 68035860 d0012b00
     */
    while (p <= end) {
        int i;

        LOG(" %08x  ", p);
        for (i = 0; i < 4; i++) {
            data = ptrace(PTRACE_PEEKTEXT, pid, (void*)p, NULL);
            LOG(" %08x", data);
            p += 4;
        }
        LOG("\n", p);
    }

    p = sp - 64;
    p &= ~3;
    if (unwind_depth != 0) {
        if (unwind_depth < STACK_CONTENT_DEPTH) {
            end = sp_list[unwind_depth-1];
        }
        else {
            end = sp_list[STACK_CONTENT_DEPTH-1];
        }
    }
    else {
        end = sp | 0x000000ff;
        end += 0xff;
    }
    LOG("\n");

    LOG("[stack dump]\n");

    /* If the crash is due to PC == 0, there will be two frames that
     * have identical SP value.
     */
    if (sp_list[0] == sp_list[1]) {
        sp_depth = 1;
    }
    else {
        sp_depth = 0;
    }

    while (p <= end) {
         char *prompt; 
         char level[16];
         data = ptrace(PTRACE_PEEKTEXT, pid, (void*)p, NULL);
         if (p == sp_list[sp_depth]) {
             sprintf(level, "#%02d", sp_depth++);
             prompt = level;
         }
         else {
             prompt = "   ";
         }
         
         LOG("%s %08x  %08x  %s\n", prompt, p, data, 
              map_to_name(map, data, ""));
         p += 4;
    }
    /* print another 64-byte of stack data after the last frame */

    end = p+64;
    while (p <= end) {
         data = ptrace(PTRACE_PEEKTEXT, pid, (void*)p, NULL);
         LOG("    %08x  %08x  %s\n", p, data, 
              map_to_name(map, data, ""));
         p += 4;
    }

    LOG("\n");
}

void dump_pc_and_lr(int pid, mapinfo *map, int unwound_level)
{
    struct pt_regs r;

    if(ptrace(PTRACE_GETREGS, pid, 0, &r)) {
        LOG("pid %d not responding!\n", pid);
        return;
    }

    if (unwound_level == 0) {
        LOG("         #%02d  pc %08x  %s\n", 0, r.ARM_pc,
             map_to_name(map, r.ARM_pc, "<unknown>"));
    }
    LOG("         #%02d  lr %08x  %s\n", 1, r.ARM_lr,
            map_to_name(map, r.ARM_lr, "<unknown>"));
}

#define MAX_LOG_TAIL_TO_SAVE	4000

void dump_klog_tail()
{
	char *buffer;
	char *start;
	int size;
	int len;

        LOG("[kernel log]\n");

	size = klogctl(10, NULL, 0);
	buffer = malloc(size);
	if(!buffer) {
		return;
	}
	size = klogctl(3, buffer, size);
	start = buffer;
	
	/* put tail end of buffer into log */
	if (size>MAX_LOG_TAIL_TO_SAVE) {
		len = MAX_LOG_TAIL_TO_SAVE;
		start = buffer+size-len;
	} else {
		len = size;
	}

	/* FIXTHIS - it would be good to filter out crash_handler
 	 * log messages here
 	 */
	write(report_fd, start, len);
	free(buffer);
}


void dump_crash_report(unsigned pid, mapinfo *milist)
{
    unsigned int sp_list[STACK_CONTENT_DEPTH];
    int stack_depth;
    int frame0_pc_sane = 1;
    
    parse_exidx_info(pid, milist);

    /* Clear stack pointer records */
    memset(sp_list, 0, sizeof(sp_list));

    LOG("[call stack]\n");

#if USE_TABLE_UNWINDER
    LOG("= table unwinder =\n");
    stack_depth = table_unwind_backtrace_with_ptrace(pid, milist, sp_list,
                                               &frame0_pc_sane);
    DLOG("stack_depth=%d\n", stack_depth);
#endif

#if USE_GUESS_UNWINDER
    memset(sp_list, 0, sizeof(sp_list));
    LOG("= best-guess unwinder =\n");
    stack_depth = guess_unwind_backtrace_with_ptrace(pid, milist, sp_list,
                                               &frame0_pc_sane);
    DLOG("stack_depth=%d\n", stack_depth);
#endif

#if USE_MCTERNAN_UNWINDER
    /* FIXTHIS - could put mcternan unwinder here */
    LOG("= McTernan unwinder =\n");
    LOG("Unwinder not integrated yet... - sorry\n");
#endif

    LOG("\n");

    /* If stack unwinder fails, use the default solution to dump the stack
     * content.
     */

    /* The stack unwinder should at least unwind two levels of stack. If less
     * level is seen we make sure at least pc and lr are dumped.
     */
    if (stack_depth < 2) {
        dump_pc_and_lr(pid, milist, stack_depth);
    }

    dump_stack_and_code(pid, milist, stack_depth, sp_list, frame0_pc_sane);

    dump_klog_tail();
}

int generate_crash_report(pid_t pid, unsigned sig, unsigned uid, unsigned gid)
{
    mapinfo *milist;
    int attach_status = -1;
    int result, status;

    dump_task_info(pid, sig, uid, pid); /* uses /proc */

    LOG("[memory maps]\n");
    /* get_mapinfo_list retrieves list and outputs to LOG */
    milist = get_mapinfo_list(pid); /* uses /proc */
    LOG("\n");

    attach_status = ptrace(PTRACE_ATTACH, pid, 0, 0);
    if(attach_status < 0) {
        LOG("crash_handler: ptrace attach failed: %s\n", strerror(errno));
    } else {
	DLOG("ptrace attach to pid %d succeeded\n", pid);
    }

    if (sig) {
	dump_fault_addr(pid, sig); /* uses ptrace */
    }

    dump_registers(pid); /* uses ptrace */
    dump_pc_code(pid); /* uses ptrace */

    dump_crash_report(pid, milist);
    
    LOG("--- done ---\n");
    
    if (attach_status == 0 ) {
	int detach_status;
	detach_status = ptrace(PTRACE_DETACH, pid, 0, 0);
    }
    free_mapinfo_list(milist);
}


int main(int argc, char *argv[])
{
    int tot, j;
    ssize_t nread;
    char buf[BUF_SIZE];
    FILE *fp;
    pid_t pid;
    unsigned int sig;
    unsigned int uid;
    unsigned int gid;
    char path[128];
    int core_out_fd;

    /* check for install argument */
    if (argc==2 && strcmp(argv[1], "--install")==0) {
        char actualpath[PATH_MAX];
        char *ptr;

	fp = fopen("/proc/sys/kernel/core_pattern", "w");
	if (!fp) {
		perror("Could not open core_pattern for installation\n");
		exit(1);
	}
        ptr = realpath(argv[0], actualpath);
        if (!ptr) {
            fprintf(stderr, "Couldn't find real path for %s\n", argv[0]);
	} else {
	    /* set the core_pattern */
	    fprintf(fp, "|%s %%p %%s %%u %%g\n", actualpath);
	    fclose(fp);
	}
	fp = fopen("/proc/sys/kernel/core_pipe_limit", "w");
	if (!fp) {
		perror("Could not open core_pipe_limit for installation\n");
		exit(1);
	}
	fprintf(fp, "1\n", "w");
	fclose(fp);
	printf("Installation done\n");
	return 0;
    }
    if (argc==2 && strcmp(argv[1], "--version")==0) {
	printf("crash_handler v%d.%d\n", VERSION, REVISION);
	return 0;
    }

    if (argc<3) {
        printf("Usage: crash_handler <pid> <sid> <uid> <gid>\n\n");
	printf("Under normal usage, the crash_handler is called directly\n");
	printf("by the Linux kernel, as is passed paramters as specified\n");
	printf("by /proc/sys/kernel/core_pattern.\n\n");

	printf("However, a few convenience options are provided:\n");
	printf("--install   Install crash_handler (register with kernel).\n");
	printf("            That is, to install the crash_handler program\n");
	printf("            on a system, copy the program to /tmp and do:\n");
	printf("              $ /tmp/crash_handler --install\n");
	printf("--version   show version information\n\n");
	return -1;
    }

    /* parse args from command line */
    pid = atoi(argv[1]);
    sig = atoi(argv[2]);
    uid = atoi(argv[3]);
    gid = atoi(argv[4]);

    report_fd = find_and_open_crash_report();

    /* start of crash handling stuff */
    /* this MUST be done before reading the core from standard in */
    generate_crash_report(pid, sig, uid, gid);

#if DO_CORE_FILE
    /* save the core file, alongside the crash_report file */
    snprintf(path, sizeof(path), CRASH_REPORT_DIR"/core_%02d", ts_num);
    core_out_fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);

    /* Count bytes in standard input (the core dump) */
    tot = 0;
    while ((nread = read(STDIN_FILENO, buf, BUF_SIZE)) > 0)
    {
	if (core_out_fd>=0) {
	    write(core_out_fd, buf, nread);
	}
        tot += nread;
    }
    fprintf(fp, "Total bytes in core dump: %d\n", tot);
    LOG("Total bytes in core dump: %d\n", tot);
    if (core_out_fd >= 0) {
	close(core_out_fd);
    }
#endif	/* DO_CORE_FILE */

    if( report_fd >= 0 ) {
	close(report_fd);
    }

    exit(EXIT_SUCCESS);
}
