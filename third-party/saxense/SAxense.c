// SixAxis Sense

/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/mman.h>

#define REPORT_SIZE	141
#define REPORT_ID	0x32
#define SAMPLE_SIZE	64
#define SAMPLE_RATE	3000

uint32_t crc32(const uint8_t* data, size_t size) {
	uint32_t crc = ~0xEADA2D49;  // 0xA2 seed

	while (size--) {
		crc ^= *data++;
		for (unsigned i = 0; i < 8; i++)
			crc = ((crc >> 1) ^ (0xEDB88320 & -(crc & 1)));
	}

	return ~crc;
}

typedef struct __attribute__((packed)) packet {
	uint8_t pid :6;
	bool unk :1,
	     sized :1;
	uint8_t length;
	uint8_t data[];
} packet_t;

struct __attribute__((packed)) report {
	uint8_t report_id;
	union {
		struct __attribute__((packed)) {
			uint8_t tag :4,
			        seq :4;
			uint8_t data[];
		};
		struct __attribute__((packed)) {
			uint8_t payload[REPORT_SIZE - sizeof(uint32_t)];
			uint32_t crc;
		};
	};
} *report;

uint8_t *sample, *ii;

static void proc(int) {
	fwrite_unlocked(report, sizeof(*report), 1, stdout);

	if (!fread_unlocked(sample, sizeof(*sample), SAMPLE_SIZE, stdin)) exit(0);

	(*ii)++;
	report->crc = crc32((void*)report, 1+sizeof(report->payload));
}

int main() {
	setbuf(stdin, NULL);
	setbuf(stdout, NULL);
	mlockall(MCL_CURRENT | MCL_FUTURE);

	static const packet_t packet_0x11 = {
		.pid = 0x11,
		.sized = true,
		.length = 7,
		.data = {0b11111110, 0, 0, 0, 0, 0xFF, 0},
	}, packet_0x12 = {
		.pid = 0x12,
		.sized = true,
		.length = SAMPLE_SIZE,
		.data = {[0 ... SAMPLE_SIZE-1] = 0},
	};

	report = malloc(sizeof(*report));
	report->report_id = REPORT_ID;
	report->tag = 0;

	packet_t *packets[] = {
		(void*)(report->data + 0),
		(void*)(report->data + sizeof(packet_0x11)+packet_0x11.length),
	};
	memcpy(packets[0], &packet_0x11, sizeof(packet_0x11)+packet_0x11.length);
	memcpy(packets[1], &packet_0x12, sizeof(packet_0x12));

	ii = &packets[0]->data[6];
	sample = packets[1]->data;

	struct itimerspec ts = {0};
	ts.it_interval.tv_nsec = 1000000000UL*SAMPLE_SIZE/(SAMPLE_RATE*2);
	ts.it_value.tv_nsec = 1;

	timer_t timerid;
	struct sigevent se = {0};
	se.sigev_notify = SIGEV_SIGNAL;
	se.sigev_signo = SIGRTMIN;
	se.sigev_value.sival_ptr = &timerid;

	signal(SIGRTMIN, proc);
	timer_create(CLOCK_MONOTONIC, &se, &timerid);
	timer_settime(timerid, 0, &ts, NULL);

	for (;;) sleep(3600);
}

// by Sdore, 2025
//  www.sdore.me
