/* usbmon.c
 *
 * Copyright 2011 Brian Swetland <swetland@frotz.net>
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <linux/ioctl.h>

typedef unsigned long long u64;
typedef signed long long s64;
typedef int s32;
typedef unsigned short u16;

#include "usbmon.h"

static char _xfer[4] = "SICB";

int main(int argc, char **argv) 
{
	unsigned char data[4096];
	struct usbmon_packet hdr;
	struct usbmon_get arg;
	unsigned char filter_dev[128];
	int fd, r, n;
	unsigned busmask = 0;

	memset(filter_dev, 0, sizeof(filter_dev));

	fd = open("/dev/usbmon0", O_RDONLY);
	if (fd < 0) 
		return -1;

	argc--;
	argv++;
	while (argc--) {
		if (argv[0][0] == '-') {
			switch(argv[0][1]) {
			case 'x': /* eXclude device */
				r = atoi(argv[0] + 2);
				if ((r < 0) || (r > 127))
					continue;
				filter_dev[r] = 1;
				break;
			case 'b': /* limit to Bus */
				r = atoi(argv[0] + 2);
				busmask = ~(1 << r);
				break;
			case 'd': /* limit to Device */
				memset(filter_dev, 0x01, sizeof(filter_dev));
				r = atoi(argv[0] + 2);
				if ((r < 0) || (r > 127))
					continue;
				filter_dev[r] = 0;
				break;
			}
		}
		argv++;
	}

	arg.hdr = &hdr;
	arg.data = data;
	for (;;) {
		arg.alloc = sizeof(data);
		r = ioctl(fd, MON_IOCX_GET, &arg);
		if (r < 0)
			break;
		if (filter_dev[hdr.devnum])
			continue;
		if (busmask & (1 << hdr.busnum))
			continue;
		printf("%d.%03d.%03d %c %c%c %04x",
			hdr.busnum, hdr.devnum, hdr.epnum & 0x7F,
			hdr.type,
			_xfer[hdr.xfer], (hdr.epnum & 0x80) ? 'i' : 'o',
#if 0
			hdr.flag_setup ? hdr.flag_setup : ' ',
			hdr.flag_data ? hdr.flag_data : ' ',
#endif
			hdr.length);
		if (hdr.type == 'S') {
			if (hdr.xfer == 2) {
				printf(" %02x %02x %02x%02x %02x%02x %02x%02x\n",
					hdr.s.setup[0], hdr.s.setup[1],
					hdr.s.setup[3], hdr.s.setup[2],
					hdr.s.setup[5], hdr.s.setup[4],
					hdr.s.setup[7], hdr.s.setup[6]);
			} else {
				goto dumpdata;
			}
	
		} else {
			switch (hdr.status) {
			case 0:
				printf(" OK\n");
				break;
			case -EPIPE:
				printf(" STALLED\n");
				break;
			case -ENODEV:
				printf(" DISCONNECTED\n");
				break;
			case -ETIMEDOUT:
				printf(" TIMEDOUT\n");
				break;
			case -EREMOTEIO:
				printf(" OK (SHORT)\n");
				break;
			default:
				printf(" %s (%d)\n", strerror(-hdr.status),-hdr.status);
			}
		}
		if (!hdr.len_cap) 
			continue;
		printf("                   ");
dumpdata:
		if (hdr.len_cap > sizeof(data))
			hdr.len_cap = sizeof(data);
		for (n = 0; n < hdr.len_cap; n++) 
			printf((n & 3) ? " %02x" : " %02x",data[n]);
		printf("\n");
		fflush(stdout);
	}
	return 0;	
}

