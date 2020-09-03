/* crash_handler.h
**
** Copyright 2011,2012 Sony Network Entertainment
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

#ifndef __crash_handler_h
#define __crash_handler_h

#define CRASH_HANDLER_DEBUG 0

#include "utility.h" /* needed for mapinfo */

extern int report_fd;
extern void report_out(int rfd, const char *fmt, ...);
extern mapinfo stack_map;
extern void klog_fmt(const char *fmt, ...);

#define LOG(fmt...) report_out(report_fd, fmt)
#if CRASH_HANDLER_DEBUG
/* choose either tombstone or klog output for debug
 * messages, by uncommenting one of the following
 */

/* #define DLOG(fmt...) report_out(report_fd, fmt) */
#define DLOG(fmt...) klog_fmt(fmt);
#else
#define DLOG(fmt...) do {} while (0)
#endif

#endif
