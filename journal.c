/*
 * journal.c - compressed crash journal, used for intelligent crash
 *    handling
 *
 ************************
 * journal format:
 * start=<time and date>
 * total=<count>
 * <pid> <name> <count> <time and date1> <time and date2> ...
 * <pid> <name> <count> <time and date1> <time and date2> ...
 */

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include "crash_handler.h"

#define MAX_PROC_NAME		64
#define MAX_LAST_CRASH		3
#define MAX_CRASH_RECORDS	20
#define MAX_BUFFER		4096

char buffer[MAX_BUFFER];

struct crash_record_struct {
	int pid;
	char pname[MAX_PROC_NAME];
	int count;
	int last_count;
	time_t last_crash[MAX_LAST_CRASH];
};

struct crash_journal_struct {
	time_t journal_start;
	int count;
	int rec_count;
	struct crash_record_struct crash_record[MAX_CRASH_RECORDS];
} crash_journal;

/* read a single process record from the crash journal */
void read_record(int i, char *line)
{
	struct crash_record_struct *cr = &crash_journal.crash_record[i];
	struct tm tm;
	char *token, *saveptr;
	int j;

	DLOG("in read_record, index=%d, line='%s'\n", i, line);

	/* read pid */
	token = strtok_r(line, " ", &saveptr);
	if (token==NULL) {
		return;
	}
	cr->pid = atoi(token);

	/* read name */
	token = strtok_r(NULL, " ", &saveptr);
	if (token==NULL) {
		return;
	}
	strncpy(cr->pname, token, MAX_PROC_NAME);
	cr->pname[MAX_PROC_NAME-1]=0;

	/* read count */
	token = strtok_r(NULL, " ", &saveptr);
	if (token==NULL) {
		return;
	}
	cr->count = atoi(token);

	/* now read some last_times */
	j = 0;
	token = strtok_r(NULL, " ", &saveptr);
	while (token!=NULL && j<MAX_LAST_CRASH) {
		DLOG("last_crash token=%s\n", token);
		strptime(token, "%Y-%m-%d-%H:%M:%S", &tm);
		cr->last_crash[j++] = mktime(&tm);
		DLOG("last crash[%d]=%d\n", j-1, cr->last_crash[j-1]);
		token = strtok_r(NULL, " ", &saveptr);
	}
	cr->last_count = j;
	
	crash_journal.rec_count += 1;
	return;
}

int read_crash_journal(char *filename)
{
	int ccode;
	int fd;
	int rec_index;
	struct crash_journal_struct *cj = &crash_journal;
	char *str1, *str2, *token, *subtoken;
	char *saveptr1, *saveptr2;
	int count;
	struct tm tm;

	ccode = -1;

	/* initialize journal with current time and 0 count, in case
	 * read fails
	 */
	cj->journal_start = time(NULL);
	cj->count = 0;
	cj->rec_count = 0;

	fd = open(filename, O_RDONLY);
	if (fd<0) {
		DLOG("problem opening %s\n", filename);
		return ccode;
	}
	count = read(fd, buffer, MAX_BUFFER);
	if(count<=0) {
		DLOG("problem reading %s\n", filename);
		return ccode;
	}
	buffer[count]=0;
	
	/* read start */
	token = strtok_r(buffer, " \n", &saveptr1);
	if (token==NULL) {
		return ccode;
	}
	DLOG("start token=%s\n", token);
	subtoken = strtok_r(token, "=", &saveptr2);
	if (subtoken==NULL) {
		return ccode;
	}
	subtoken = strtok_r(NULL, "=", &saveptr2);
	if (subtoken==NULL) {
		return ccode;
	}
	strptime(subtoken, "%Y-%m-%d-%H:%M:%S", &tm);
	cj->journal_start = mktime(&tm);

	/* now read count */
	token = strtok_r(NULL, " \n", &saveptr1);
	if (token==NULL) {
		return ccode;
	}
	DLOG("count token=%s\n", token);
	subtoken = strtok_r(token, "=", &saveptr2);
	if (subtoken==NULL) {
		return ccode;
	}
	subtoken = strtok_r(NULL, "=", &saveptr2);
	if (subtoken==NULL) {
		return ccode;
	}
	cj->count = atoi(subtoken);

	/* read records */
	rec_index = 0;
	while (token != NULL && rec_index<MAX_CRASH_RECORDS) { 
		token = strtok_r(NULL, "\n", &saveptr1);
		read_record(rec_index, token);
		rec_index++;
	}
}

int write_crash_journal(char *filename)
{
	int ccode;
	int fd;
	struct crash_journal_struct *cj = &crash_journal;
	struct crash_record_struct *cr;
	size_t fmt_count;
	int count;
	int i, j;

	fd = open(filename, O_CREAT|O_TRUNC|O_WRONLY,
		S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	if (fd<0) {
		DLOG("problem '%s' writing journal\n", strerror(errno));
		return fd;
	}
	
	fmt_count = strftime(buffer, MAX_BUFFER,
		"start=%Y-%m-%d-%H:%M:%S\n", localtime(&cj->journal_start));
	count = write(fd, buffer, fmt_count);
	/* FIXTHIS - should verify amount written */

	fmt_count = sprintf(buffer, "total=%d\n", cj->count);
	count = write(fd, buffer, fmt_count);

	for(i=0; i<cj->rec_count; i++) {
		cr = &cj->crash_record[i];
		fmt_count = sprintf(buffer, "%d %s %d",
			cr->pid, cr->pname, cr->count);
		count = write(fd, buffer, fmt_count);
		for(j=0; j<cr->last_count; j++) {
			fmt_count = strftime(buffer, MAX_BUFFER,
				" %Y-%m-%d-%H:%M:%S",
				localtime(&cr->last_crash[j]));
			count = write(fd, buffer, fmt_count);
		}
		count = write(fd, "\n", 1);
	}
	return close(fd);
}

void dump_crash_journal()
{
	struct crash_journal_struct *cj = &crash_journal;
	struct crash_record_struct *cr;
	size_t fmt_count;
	int count;
	int i, j;
	
	fmt_count = strftime(buffer, MAX_BUFFER,
		"journal_start: %Y-%m-%d-%H:%M:%S\n",
		localtime(&cj->journal_start));
	DLOG(buffer);
	DLOG("count:%d\n", cj->count);
	DLOG("rec_count:%d\n", cj->rec_count);

	for (i=0; i<cj->rec_count; i++) {
		cr = &cj->crash_record[i];
		DLOG("%d %s %d (%d)\n", cr->pid, cr->pname, cr->count,
			cr->last_count);
		for(j=0; j<cr->last_count; j++) {
			fmt_count = strftime(buffer, MAX_BUFFER,
				" %Y-%m-%d-%H:%M:%S\n",
				localtime(&cr->last_crash[j]));
			DLOG(buffer);
		}
	}
}

/*******************
 * find_crash_record - find a matching crash record by name or pid
 *  matching by name and pid is only possibility.
 *  currently the code just matches by name
 *******************/
struct crash_record_struct *find_crash_record(int pid, char *name)
{
	int i;
	
	for(i=0; i<MAX_CRASH_RECORDS; i++) {
		// FIXTHIS - should allow control over what comparison
		// policy is used in find_crash_record
//		if (crash_journal.crash_record[i].pid==pid &&
//			strcmp(crash_journal.crash_record[i].pname, name)==0) {
		if (strcmp(crash_journal.crash_record[i].pname, name)==0) {
			return &crash_journal.crash_record[i];
		}
	}
	return NULL;
}

/* find the crash record with the oldest most recent entry */
struct crash_record_struct *find_oldest_crash_record()
{
	int i, j;
	struct crash_record_struct *cr, *oldest_cr;
	time_t oldest_time, newest_time_this_record;
	
	oldest_cr = NULL;
	oldest_time = time(NULL);
	for(i=0; i<MAX_CRASH_RECORDS; i++) {
		cr = &crash_journal.crash_record[i];
		newest_time_this_record = 0;	
		for( j=0; j<cr->last_count; j++ ) {
			if(cr->last_crash[j]>newest_time_this_record) {
				newest_time_this_record = cr->last_crash[j];
			}
		}
		if (oldest_time < newest_time_this_record) {
			oldest_time = newest_time_this_record;
			oldest_cr = cr;
		}
	}
	return cr;
}

struct crash_record_struct *get_crash_record(char *filename, int pid,
		char *name)
{
	struct crash_record_struct *cr;

	read_crash_journal(filename);
	cr = find_crash_record(pid, name);
	return cr;
}

void record_crash_to_journal(char *filename, int pid, char *name)
{
	struct crash_journal_struct *cj = &crash_journal;
	struct crash_record_struct *cr;
	time_t current;
	int i;

	/* read journal */
	read_crash_journal(filename);

	DLOG("in read_crash_journal, before:\n");
	dump_crash_journal();

	cj->count++;

	/* update existing entry, if found */
	/* otherwise add entry, or replace oldest if out of space */
	cr = find_crash_record(pid, name);
	if (cr != NULL) {
		cr->count++;

		/* update this record with current time */
		/* put current time at front of time list */
		int top = cr->last_count;
		if (top>=MAX_LAST_CRASH) {
			top=MAX_LAST_CRASH-1;
		}
		for(i=top; i>0; i--) {
			cr->last_crash[i] = cr->last_crash[i-1];
		}
		cr->last_crash[0] = time(NULL);
		if (cr->last_count < MAX_LAST_CRASH) {
			cr->last_count++;
		}
	} else {
		DLOG("adding an entry\n");
		/* if more room, insert an entry */
		if (cj->rec_count < MAX_CRASH_RECORDS) {
			/* move all records down */
			for(i=cj->rec_count; i>0; i--) {
				cj->crash_record[i]=cj->crash_record[i-1];
			}
			cj->rec_count++;
			cr = &cj->crash_record[0];
		} else {
			/* overwrite the oldest entry */ 
			cr = find_oldest_crash_record();
		}
		/* fill in the record */
		cr->pid = pid;
		strncpy(cr->pname, name, MAX_PROC_NAME);
		cr->pname[MAX_PROC_NAME-1] = 0;
		cr->count = 1;
		cr->last_crash[0] = time(NULL);
		cr->last_count = 1;
	}

	DLOG("after\n");
	dump_crash_journal();

	/* write journal */
	write_crash_journal(filename);
}
