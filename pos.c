#include "pos.h"
#include <stdio.h>
uint8_t val2pos(uint8_t* buffer, pos_struct* data){
	//Extract data
	uint8_t type=1;
	uint8_t sender_l_1 = data->sender[0]-65;
	uint8_t sender_l_2 = data->sender[1]-65;
	uint8_t sender_num = data->sender[2]-48;
	uint32_t time_sec = data->time;
	uint32_t northing= data->northing-6000000;
	uint32_t easting= data->easting;
	uint16_t height = (uint16_t) data->height/2;
	uint16_t speed= (uint16_t) data->speed*10;
	uint8_t angle =(uint8_t) (data->angle/6);
	uint8_t vbat = (uint8_t) ((data->v_bat-5000)/10);
	//2's complement
	uint8_t temp = 0;
	int8_t t_temp = data->temp;
	if (t_temp < 0)
		temp=(uint8_t) ((~(-t_temp))+1)&63;
	else temp=t_temp&31;
	uint8_t status = data->status;
	uint8_t sos = data->sos;
	//zero everything
	for (uint8_t i=0;i<15;i++){
		buffer[i] = 0;
	}
	//type
	uint8_t type_4=(uint8_t) type << 4;
	buffer[0] +=type_4;
	//sender 4 first bits of first letter
	uint8_t sender_l_1_4 =(uint8_t) ((sender_l_1 >> 1)&15);
	buffer[0] += sender_l_1_4;
	//sender add last bit from first letter
	buffer[1] +=(uint8_t) (sender_l_1 & 1)<<7;
	//sender add second letter
	buffer[1] +=(uint8_t) sender_l_2 <<2;
	//sender add 2 bits from num
	buffer[1] +=(uint8_t) sender_num >>2;
	//add last 2 bits of sender num
	buffer[2] +=(uint8_t) sender_num <<6;
	//add time. 6+8+3 bits
	//first 6 bits
	uint8_t time_6 = (uint8_t)(time_sec>>11)&63;
	buffer[2] += time_6;
	//full byte, bits 7-14
	uint8_t time_8 = (uint8_t) ((time_sec>>3)&255);
	buffer[3] += time_8;
	//bits 15-17
	uint8_t time_3 =(uint8_t) ((time_sec&7)<<5);
	buffer[4] += time_3;
	//northing, 21 bits in 5+8+8
	uint8_t north_5 = (uint8_t) ((northing >> 16)&31);
	uint8_t north_8 = (uint8_t) ((northing >> 8)&255);
	uint8_t north_8_2 = (uint8_t) (northing&255);
	buffer[4] += north_5;
	buffer[5] += north_8;
	buffer[6] += north_8_2;
	//easting, 8+8+4
	//123456789abcdefghijk
	//11111111222222223333
	uint8_t east_8 = (uint8_t) (easting >> 12)&255;
	uint8_t east_8_2 = (uint8_t) ((easting >> 4)&255);
	uint8_t east_4 = (uint8_t) ((easting & 15)<<4);
	buffer[7] += east_8;
	buffer[8] += east_8_2;
	buffer[9] += east_4;
	//height in 11 bits, 4+7 bits
	//123456789ab
	//11112222222
	uint8_t height_4 = (uint8_t) (height >> 7)&15;
	uint8_t height_7 = (uint8_t) ((height&127)<<1);
	buffer[9] +=height_4;
	buffer[10] +=height_7;
	//speed, 9 bits, 1 + 8
	//123456789
	//122222222
	uint8_t speed_1 = (uint8_t) (speed >> 8)&1;
	uint8_t speed_8 = (uint8_t) (speed&255);
	buffer[10] +=speed_1;
	buffer[11] += speed_8;
	//angle, 6 bits, whole
	uint8_t angle_6 = (uint8_t) angle<<2;
	buffer[12] += angle_6;
	//battery voltage, 8 bits, 2 + 6
	//12345678
	//11222222
	//xxxxxx11
	//222222xx
	uint8_t vbat_2 = (uint8_t) (vbat >> 6);
	uint8_t vbat_6 = (uint8_t) (vbat << 2);
	buffer[12] += vbat_2;
	buffer[13] += vbat_6;
	//temp, 6 bits, 2 + 4
	uint8_t temp_2 = (uint8_t) ((temp >> 4)&3);
	uint8_t temp_4 = (uint8_t) (temp << 4);
	buffer[13] += temp_2;
	buffer[14] += temp_4;
	//status, 3 bits, whole
	uint8_t  status_3 = (uint8_t) (status <<1);
	buffer[14] += status_3;
	//sos, 1 bit, whole
	buffer[14] += sos;
	return 0;

}

uint8_t pos2val(pos_struct* data,uint8_t*message){
	//type
	data->type = message[0]>>4;
	//sender
    data->sender[0] = ((message[0]&15)<<1) + ((message[1]&128)>>7)+65;
	data->sender[1] = ((message[1]&124)>>2)+65;
	data->sender[2] = ((message[1]&3)<<2)+((message[2]&192)>>6)+48;
	//time
	uint32_t t_time = 0;
	t_time += (message[2]&63)<<11;
	t_time += (message[3]<<3);
	t_time += (message[4]&224)>>5;
	data->time = t_time;
	//northing
	uint32_t t_northing = 0;
	t_northing += ((message[4]&31)<<16);
	t_northing += ((message[5])<<8);
	t_northing += message[6];
	t_northing +=6000000;
	data->northing = t_northing;
	//easting
	uint32_t t_easting=0;
	t_easting += message[7]<<12;
	t_easting += message[8]<<4;
	t_easting += (message[9]&240)>>4;
	data->easting = t_easting;
	//height
	data->height = (uint16_t) (((message[9]&15)<<7)+((message[10]&254)>>1))*2;
	//speed
	data->speed=(float)(((message[10]&1)<<8)+(message[11]))/10;
	//angle
	data->angle = (uint16_t) ((message[12]&252)>>2)*6;
	//vbat
	data->v_bat = 5000 + 10*( (uint16_t) ((message[12]&3)<<6) + ((message[13]&252)>>2) );
	//temp
	uint8_t temp_temp = ((message[13]&3)<<4 )+ ((message[14]&240)>>4);
	if (temp_temp>32)
		data->temp=-(((~temp_temp)+1)&31);
	else data->temp=(temp_temp&31);
	//status
	data->status = (message[14]&14)>>1;
	//sos
	data->sos = message[14]&1;
	return 0;
}

int pos2text(uint8_t *data, char *text, int textlen) {
	pos_struct p;
	int h,m,s,t, rssi=0;
	const char *myid = "SDR";
	pos2val(&p, data);
	t = p.time;
	h = t / 3600;
	m = (t - h*3600) / 60;
	s = t - h* 3600 - m*60;
	/*return snprintf(text, textlen, "$POS|ETRS-TM35FIN|%c%c%c|2017-08-12|%u:%u:%u|%lu|%lu|%f|%u*\n", tacmes->sender[0], tacmes->sender[1], tacmes->sender[2], h, m, s, tacmes->northing, tacmes->easting, tacmes->height, tacmes->sos);*/
	return snprintf(text, textlen,
	"$TACPOS|ETRS-TM35FIN|%c%c%c||UTC%d:%d:%d|%d|%d|%.2f|||OK|%d||%d|%s|%d|||*\n",
	p.sender[0], p.sender[1], p.sender[2],
	h, m, s,
	p.northing, p.easting, (double)p.height, p.sos, p.v_bat, myid, rssi);
}

