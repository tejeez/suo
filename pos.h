#ifndef INC_POS_H_
#define INC_POS_H_

#include <stdint.h>
#include <stdlib.h>

typedef struct {
	uint8_t sender[4];
	uint32_t time, northing, easting;
	uint8_t status,sos,type;
	int8_t temp;
	uint16_t angle,v_bat;
	float height,speed;

} pos_struct;

uint8_t val2pos(uint8_t* buffer, pos_struct* data);

uint8_t pos2val(pos_struct* data, uint8_t* message);

int pos2text(uint8_t *data, char *text, int textlen);

#endif
