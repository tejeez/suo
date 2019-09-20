#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <liquid/liquid.h>
#include "pos.h"

int bit_num = 0;

typedef uint64_t bw_t; // bit window

const bw_t
 syncword = 0b010101011111011010001101,
 syncmask = 0b111111111111111111111111;
bw_t last_bits = 0;

#define SYNC_WIN_START 40
#define SYNC_WIN_END 300
#define SYNC_THRESHOLD 40
#define PKT_BITS 304
#define MSG_DEC_BYTES 18
#define BITS_STORED (SYNC_WIN_END+PKT_BITS+100)
int syncerrs[SYNC_WIN_END];
int pbits[BITS_STORED];

uint8_t swap_byte_bit_order(uint8_t v) {
	return
	((0x80&v) >> 7) |
	((0x40&v) >> 5) |
	((0x20&v) >> 3) |
	((0x10&v) >> 1) |
	((0x08&v) << 1) |
	((0x04&v) << 3) |
	((0x02&v) << 5) |
	((0x01&v) << 7);
}

/* Correct polynomial found by brute force search... */
uint16_t pos_crc(uint8_t *data, int len) {
	int i;
	uint16_t crc = 0x0000;
	const uint16_t poly = 0xA001; // 0x6000 also works
	for (i=0; i<len; i++) {
		int j;
		uint8_t d = data[i];
		for (j=0; j<8; j++) {
			if ((d ^ crc) & 1)
				crc = (crc >> 1) ^ poly;
			else
				crc =  crc >> 1;
			d >>= 1;
		}
	}
	return crc;
}

void pos_received(uint8_t *data, int len) {
	char print_buf[1000]/*, *print_p = print_buf*/;
	int i;
	(void)len;
	i = pos2text(data, print_buf, 1000);
	printf("%s", print_buf);
	(void)write(3, print_buf, i);
	#if 0
	print_p += sprintf(print_p, "$8101241114113deaffffffffffffffffffff\n$007000");
	for(i = 0; i < len; i++) {
		print_p += sprintf(print_p, "%02x", data[i]);
	}
	print_p += sprintf(print_p, "\n");
	write(3, print_buf, print_p - print_buf);
	#endif
}

fec fecdecoder;
void pkt_received(int *pktb) {
	int i;
	/*for(i=0;i<PKT_BITS;i++) putchar('0' + pktb[i]);
	putchar('\n');*/
	// deinterleave
	const int msg_rec_bytes = (PKT_BITS+7)/8;
	uint8_t msg_rec[msg_rec_bytes], msg_dec[MSG_DEC_BYTES];
	memset(msg_rec, 0, msg_rec_bytes);
	for(i = 0; i < PKT_BITS; i++) {
		int oi;
		oi = (i&(~15)) | (15 - ((i&15)/4) - ((i&3)*4));
		oi ^= 1;
		assert(oi/8 < msg_rec_bytes);
		if(pktb[i]) msg_rec[oi/8] |= 0x80 >> (oi&7);
	}
	fec_decode(fecdecoder, MSG_DEC_BYTES, msg_rec, msg_dec);
	for(i = 0; i < MSG_DEC_BYTES; i++)
		msg_dec[i] = swap_byte_bit_order(msg_dec[i]);
	//unsigned a = crc_generate_key(LIQUID_CRC_16, msg_dec, 16);
	//printf("crc=%02x ", a);
	printf("RX ");
	for(i = 0; i < MSG_DEC_BYTES; i++) {
		printf("%02x ", msg_dec[i]);
	}
	printf("\n");
	if(msg_dec[0] == 15) {
		uint16_t crc_rx = (msg_dec[16] << 8) | msg_dec[17];
		uint16_t crc_c = pos_crc(msg_dec, 16);
		if (crc_c == crc_rx)
			pos_received(msg_dec+1, 15);
		else
			printf("CRC error %04x %04x\n", crc_rx, crc_c);
	}
	fflush(stdout);
}

int syncp = -1, running = 0;
int least_errs = 100, least_errs_p = 0;

void start_of_packet() {
	bit_num = 0;
	last_bits = 0;
	least_errs = 100;
	least_errs_p = 0;
	syncp = -1;
	running = 1;
}

void bit_received(int b) {
	int errs;
	if(!running) return;
	//putchar('a'+b);
	if(bit_num < BITS_STORED) {
		pbits[bit_num] = b;
	} else {
		running = 0;
		return;
	}
	if(bit_num < SYNC_WIN_END) {
		last_bits = (last_bits<<1) | (b&1);
		errs = __builtin_popcountll((last_bits ^ syncword) & syncmask);
		syncerrs[bit_num] = errs;
		if(errs < least_errs) {
			least_errs = errs;
			least_errs_p = bit_num;
		}
	}
	if(least_errs <= SYNC_THRESHOLD && bit_num == least_errs_p+8)
		syncp = least_errs_p+1;
	if(syncp >= 0 && bit_num >= syncp + PKT_BITS) {
		pkt_received(pbits + syncp/* -16*/);
		running = 0;
	}
	bit_num++;
}

#define READBUF 256
int main() {
	char readbuf[READBUF];
	ssize_t r, i;
	fecdecoder = fec_create(LIQUID_FEC_CONV_V27, NULL);
	for(;;) {
		r = read(0, readbuf, READBUF);
		if(r <= 0) break;
		for(i=0; i<r; i++) {
			switch(readbuf[i]) {
			case '0':
				bit_received(0); break;
			case '1':
				bit_received(1); break;
			case '\n':
				start_of_packet(); break;
			default: break;
			}
		}
	}
	return 0;
}
