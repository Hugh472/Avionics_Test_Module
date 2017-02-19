/*
 * darV3SimDefaults.h
 *
 *  Created on: Jul 29, 2015
 *      Author: khughes
 */

#ifndef DARV3SIMDEFAULTS_H_
#define DARV3SIMDEFAULTS_H_

//DARV3 datagram inside buffer
typedef struct Header_S
{
    u_int8_t hdr_top;
    u_int8_t data_type;
    u_int16_t packet_length;
    u_int8_t config_count;
    u_int8_t flags;
    u_int16_t seq_num;
    u_int32_t DSID;
    u_int32_t multicast_addr;
    u_int32_t time_stamp;
}__attribute__((packed)) NPD_Header;

typedef struct Datagram_S
{
    u_int32_t time_delta;
    u_int16_t segment_length;
    u_int8_t error_code;
    u_int8_t flags;
    u_int8_t SFID;
    u_int8_t cal_reserved_a;
    u_int16_t reserved_b;
}__attribute__((packed)) ACQ_Segment_Header;

typedef struct Properties_S
{
    int num_sync_chars;
    u_int16_t sync_char_1;
    u_int16_t sync_char_2;
    int frame_rate;
    int bits_per_word;
    int words_per_min_f;
    int min_fs_per_maj_f;
} Properties;

typedef struct Buffer_S
{
    NPD_Header darV3_header;
    ACQ_Segment_Header seg_header;
} Buff;

typedef struct Threaded_S
{
    int head_size;
    int pay_size;
    int pack_size;
} Tx_Thread;
typedef struct Statistics_s
{
    uint32_t packet_num, packet_rate, packet_mod;
    uint32_t byte_num, byte_rate, byte_mod;
    uint32_t start_sec, start_usec;
    uint32_t time_total;
} Stats;

#define DARV3SIM_DARHEADER_SIZE sizeof(NPD_Header)
#define DARV3SIM_SEGHEADER_SIZE sizeof(ACQ_Segment_Header)

#define DARV3SIM_DARHEAD_VERSION_DEFAULT 0x3
#define DARV3SIM_DARHEAD_BITSHIFT 4 //bitshift and or-in are for
#define DARV3SIM_DARHEAD_OR_IN 0x5
#define DARV3SIM_DARHEAD_DATATYPE 0xA1 //MACQ
#define DARV3SIM_DARHEAD_CONFIGCOUNT 0x80
#define DARV3SIM_DARHEAD_FLAGS 0x6 //
#define DARV3_DSID_DEFAULT 0x63
#define DARV3SIM_MCADDR_DEFAULT "238.0.1.42" // required: 238.x.1.x

#define DARV3SIM_SEGHEAD_ERRORCODE 0x0
#define DARV3SIM_SEGHEAD_FLAGS 0x1
#define DARV3SIM_SEGHEAD_SFID 0x00
#define DARV3SIM_SEGHEAD_CALRES_A 0x0
#define DARV3SIM_SEGHEAD_CALRES_BITSHIFT 5
#define DARV3SIM_SEGHEAD_CALRES_B htons(0x0000)

#define DARV3SIM_PROPS_FRAMERATE 100
#define DARV3SIM_PROPS_NUM_SYNCCHARS_DEFAULT 2
#define DARV3SIM_PROPS_SYNCCHAR_DEFAULT 0x7B90
#define DARV3SIM_PROPS_BITSPWORD_DEFAULT 16
#define DARV3SIM_PROPS_WORDSPMINF_DEFAULT 4
#define DARV3SIM_PROPS_MINFSPMAJF 1

#define DARV3SIM_SEQNUM_START 1
#define DARV3SIM_TTL_DEFAULT 1
#define DARV3SIM_SO_REUSEADDR_DEFAULT 1
#define DARV3SIM_SENDBUFF_DEFAULT 65536
#define DARV3SIM_LOOPCH_DEFAULT 0

#define DARV3SIM_DAR_PORT 50001 // to be recognized as a darV3
#define DARV3SIM_PTHREAD_COUNT 0

#endif /* DARV3SIMDEFAULTS_H_ */
