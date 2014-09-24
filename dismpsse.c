// Copyright 2014 Brian Swetland <swetland@frotz.net>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

#define TRACE_MPSSE	0
#define TRACE_JTAG	0

// ----- simulate arm dap -----

void dap_abort(u64 v) {
	u32 data = v;
	printf("abort wr %08x\n", data); // 36?
}

u32 dap_apnum = 0;
u32 dap_bank = 0;

const char *dp_addr2name(unsigned addr) {
	switch (addr) {
	case 0x00: return "ABORT";
	case 0x04: return "CSW";
	case 0x08: return "SELECT";
	case 0x0C: return "RDBUFF";
	default: return "XXX";
	}
}

void dap_dpacc(u64 v) {
	u32 data = v >> 3;
	u32 cmd = v & 7;
	u32 addr = (cmd & 6) << 1;

	printf("dpacc %s %08x -> DP  %02x %s\n",
		(cmd & 1) ? "rd" : "wr", data, addr, dp_addr2name(addr));

	if (addr == 0x08) { // DPSEL
		dap_apnum = data >> 24;
		dap_bank = data & 0xF0;
	}
}

const char *ap_addr2name(unsigned addr) {
	switch (addr) {
	case 0x00: return "CSW";
	case 0x04: return "TAR";
	case 0x0C: return "DRW";
	case 0x10: return "BD0";
	case 0x14: return "BD1";
	case 0x18: return "BD2";
	case 0x1C: return "BD3";
	case 0xF4: return "CFG";
	case 0xF8: return "BASE";
	case 0xFC: return "IDR";
	default: return "XXX";
	}
}

void dap_apacc(u64 v) {
	u32 data = v >> 3;
	u32 cmd = v & 7;
	u32 addr = dap_bank | ((cmd & 6) << 1);
	printf("apacc %s %08x -> AP%d %02x %s\n",
		(cmd & 1) ? "rd" : "wr", data, dap_apnum, addr,
		ap_addr2name(addr));
}


// ----- simulate zynq jtag chain --------

// TDI -> ARM(4) -> FPGA(6) -> TDO
u32 ir_fpga = 0xffffffff;
u32 ir_arm = 0xffffffff;

void sim_ir(u64 data) {
	ir_fpga = data & 0x3f;
	ir_arm = (data >> 6) & 0xf;
//	printf("ARM(%02x) FPGA(%02x)\n", ir_arm, ir_fpga);
}

void sim_dr(u64 data) {
	// if fpga's not in bypass, no idea what we're doing
	if (ir_fpga != 0x3f) return;
	// discard the bit being fed into fpga's bypass register
	data >>= 1;

	switch (ir_arm) {
	case 0x08: dap_abort(data); break;
	case 0x0a: dap_dpacc(data); break;
	case 0x0b: dap_apacc(data); break;
	}
}

// ----- simulate jtag core -----

#define JTAG_RESET      0
#define JTAG_IDLE       1
#define JTAG_DRSELECT   2
#define JTAG_DRCAPTURE  3
#define JTAG_DRSHIFT    4
#define JTAG_DREXIT1    5
#define JTAG_DRPAUSE    6
#define JTAG_DREXIT2    7
#define JTAG_DRUPDATE   8
#define JTAG_IRSELECT   9
#define JTAG_IRCAPTURE  10
#define JTAG_IRSHIFT    11
#define JTAG_IREXIT1    12
#define JTAG_IRPAUSE    13
#define JTAG_IREXIT2    14
#define JTAG_IRUPDATE   15

static const char *JSTATE[16] = {
        "RESET", "IDLE", "DRSELECT", "DRCAPTURE",
        "DRSHIFT", "DREXIT1", "DRPAUSE", "DREXIT2",
        "DRUPDATE", "IRSELECT", "IRCAPTURE", "IRSHIFT",
        "IREXIT1", "IRPAUSE", "IREXIT1", "IRUPDATE"
};

struct {
	u8 next0;
	u8 next1;
} GRAPH[16] = {
	[JTAG_RESET] = { JTAG_IDLE, JTAG_RESET },
	[JTAG_IDLE] = { JTAG_IDLE, JTAG_DRSELECT },
	[JTAG_DRSELECT] = { JTAG_DRCAPTURE, JTAG_IRSELECT },
	[JTAG_DRCAPTURE] = { JTAG_DRSHIFT, JTAG_DREXIT1 },
	[JTAG_DRSHIFT] = { JTAG_DRSHIFT, JTAG_DREXIT1 },
	[JTAG_DREXIT1] = { JTAG_DRPAUSE, JTAG_DRUPDATE },
	[JTAG_DRPAUSE] = { JTAG_DRPAUSE, JTAG_DREXIT2 },
	[JTAG_DREXIT2] = { JTAG_DRSHIFT, JTAG_DRUPDATE },
	[JTAG_DRUPDATE] = { JTAG_IDLE, JTAG_DRSELECT },
	[JTAG_IRSELECT] = { JTAG_IRCAPTURE, JTAG_RESET },
	[JTAG_IRCAPTURE] = { JTAG_IRSHIFT, JTAG_IREXIT1 },
	[JTAG_IRSHIFT] = { JTAG_IRSHIFT, JTAG_IREXIT1 },
	[JTAG_IREXIT1] = { JTAG_IRPAUSE, JTAG_IRUPDATE },
	[JTAG_IRPAUSE] = { JTAG_IRPAUSE, JTAG_IREXIT2 },
	[JTAG_IREXIT2] = { JTAG_IRSHIFT, JTAG_IRUPDATE },
	[JTAG_IRUPDATE] = { JTAG_IDLE, JTAG_DRSELECT },
};

unsigned state = JTAG_RESET;
unsigned shiftcount = 0;
unsigned idlecount = 0;
u64 shiftdata = 0;

void _sim_jtag(unsigned tdi, unsigned tms) {
	switch (state) {
	case JTAG_IDLE:
		idlecount++;
		break;
	case JTAG_DRSHIFT:
	case JTAG_IRSHIFT:
		if (shiftcount < 64) {
			if (tdi) shiftdata |= (1ULL << shiftcount);
		} else {
			shiftdata >>= 1;
			if (tdi) shiftdata |= 0x8000000000000000ULL;
		}
		shiftcount++;
		break;
	case JTAG_DRCAPTURE:
	case JTAG_IRCAPTURE:
		shiftcount = 0;
		shiftdata = 0;
		break;
	case JTAG_DRUPDATE:
#if TRACE_JTAG
		printf("jtag: %4d -> DR %016lx\n", shiftcount, shiftdata);
#endif
		sim_dr(shiftdata);
		break;
	case JTAG_IRUPDATE:
#if TRACE_JTAG
		printf("jtag: %4d -> IR %016lx\n", shiftcount, shiftdata);
#endif
		sim_ir(shiftdata);
		break;
	};
#if TRACE_JTAG
	printf("jtag: state = %s\n", JSTATE[state]);
#endif
	state = tms ? GRAPH[state].next1 : GRAPH[state].next0;
}

u8 stream[1024*1024];
unsigned scount = 0;
unsigned last_tms;

void sim_jtag(void) {
	u8 *x = stream;
	while (scount-- > 0) {
		_sim_jtag(*x & 1, *x >> 1);
		x++;
	}
}

void wr_jtag(unsigned tdi, unsigned tms) {
	if (scount == sizeof(stream)) {
		fprintf(stderr,"OVERFLOW\n");
		exit(-1);
	}
	stream[scount++] = tdi | (tms << 1);
	last_tms = tms;
}

void wr_tdi(unsigned count, unsigned bits) {
	while (count > 0) {
		wr_jtag(bits & 1, last_tms);
		bits >>= 1;
		count--;
	}	
}

void wr_tms(unsigned count, unsigned bits, unsigned tdi) {
	while (count > 0) {
		wr_jtag(tdi, bits & 1);
		bits >>= 1;
		count--;
	}
}

// ----- disassemble mpsse stream into tdi/tms jtag vectors -----

#if TRACE_MPSSE
#define dprintf(x...) printf(x)
#else
#define dprintf(x...) do {} while(0)
#endif

static void pbin(u32 val, u32 bits) {
	u32 n;
	for (n = 0; n < bits; n++) {
		dprintf( "%c", (val & 1) ? '1' : '0');
		val >>= 1;
	}
}

// display mpsse command stream in a (sortof) human readable form
static void dismpsse(u8 *data, u32 n) {
	u32 x, i;
	while (n > 0) {
		dprintf("%02x: ", data[0]);
		switch(data[0]) {
		case 0x6B: // tms rw
		case 0x6F: // tms rw -veWr -veRd
			dprintf( "x1 <- TDO, ");
			// fall through
		case 0x4B: // tms wo
			dprintf( "TMS <- ");
			pbin(data[2],data[1]+1);
			dprintf( ", TDI <- ");
			pbin((data[2] & 0x80) ? 0xFF : 0, data[1] + 1);
			dprintf( "\n");
			wr_tms(data[1] + 1, data[2], (data[2] & 0x80) ? 1 : 0);
			data += 3;
			n -= 3;
			break;
		case 0x2A: // ro bits
			dprintf( "x%d <- TDO\n", data[1] + 1);
			data += 2;
			n -= 2;
			break;
		case 0x28: // ro bytes
		case 0x2C: // ro bytes -veWr -veRd
			x = ((data[2] << 8) | data[1]) + 1;
			dprintf( "x%d <- TDO\n", (int) x * 8);
			data += 3;
			n -= 3;
			break;
		case 0x1B: // wo bits
		case 0x3B: // rw bits
		case 0x3F: // rw bits -veWR -veRD
			dprintf( "TDI <- ");
			pbin(data[2], data[1] + 1);
			if (data[0] == 0x3B) {
				dprintf( ", x%d <- TDO\n", data[1] + 1);
			} else {
				dprintf( "\n");
			}
			wr_tdi(data[1] + 1, data[2]);
			data += 3;
			n -= 3;
			break;
		case 0x19: // wo bytes
		case 0x39: // rw bytes
		case 0x3D: // rw bytes -veWR -veRD
			x = ((data[2] << 8) | data[1]) + 1;
			dprintf( "TDI <- ");
			for (i = 0; i < x; i++) pbin(data[3+i], 8);
			if (data[0] == 0x1B) {
				dprintf( ", x%d <- TDO\n", (int) x);
			} else {
				dprintf("\n");
			}
			for (i = 0; i < x; i++) wr_tdi(8, data[3+i]);
			data += (3 + x);
			n -= (3 + x);
			break;
		case 0x87:
			dprintf("FLUSH\n");
			data += 1;
			n -= 1;
			break;
		case 0xAA:
		case 0xAB:
			dprintf("BADCMD\n");
			data += 1;
			n -=1;
			break;
		case 0x80:
		case 0x82:
			dprintf("SET%s %02x %02x\n", (data[0] & 2) ? "HI" : "LO", data[1], data[2]);
			data += 3;
			n -= 3;
			break;
		case 0x81:
		case 0x83:
			dprintf("READ%s\n", (data[0] & 2) ? "HI" : "LO");
			data += 1;
			n -= 1;
			break;
		case 0x84:
		case 0x85:
			dprintf("LOOPBACK %s\n", (data[0] & 1) ? "OFF" : "ON");
			data += 1;
			n -= 1;
			break;
		case 0x86:
			dprintf("CLOCKDIV %d\n", (data[1] | (data[2] << 8)));
			data += 3;
			n -= 3;
			break;
		case 0x8A:
		case 0x8B:
			dprintf("DIVBY5 %s\n", (data[0] & 1) ? "ENABLE" : "DISABLE");
			data += 1;
			n -= 1;
			break;
		default:
			fprintf(stderr, "INVALID OPCODE %02x\n",data[0]);
			n = 0;
		}
	}
}

u8 buffer[1024*1024];

int main(int argc, char **argv) {
	char hex[32];
	int n, c, count;

	n = 0;
	count = 0;
	for (;;) {
		if ((c = getchar()) < 0) break;
		if (isspace(c)) {
			if (n != 2) {
				printf("bad input\n");
				return -1;
			}
			hex[n] = 0;
			buffer[count++] = strtoul(hex, 0, 16);
			n = 0;
			continue;
		}
		if (n == 2) {
			printf( "bad input\n");
			return -1;
		}
		hex[n++] = c;
	}
	printf("count %d\n", count);
	dismpsse(buffer, count);
	printf("scount %d\n", scount);
	sim_jtag();
	return 0;
	
}

// notes
//
// extract bulk out data from usbmon dump:
// grep " Bo " LOG | grep -v OK | sed 's/.* Bo .... //'

