/*
 * darV3Sim
 * author: Kyle Hughes
 * current as of 08/13/15
 */
#include "darV3SimIncludes.h"
#include "darV3SimDefaults.h"

static Buff _darV3SimDummyData;
static Properties _darV3SimProps;
static u_int8_t *_darV3SimEntirePacket;
static uint32_t _darV3SimMsPerOsTick;
static uint32_t _darV3SimFramesPerTick, _darV3SimFramesPerTickFrac;
static clock_t _darV3SimPre, _darV3SimCurrent;
static int32_t _darV3SimOsTicksPerSec, _darV3SimOsTickFraction;
static int32_t _darV3SimUDPSocket;
static int32_t _darV3SimInc = 0x1;
static int32_t _darV3SimExit = 0;
static struct sockaddr_in _darV3SimSourceAddr, _darV3SimDestAddr;
static struct timeval _darV3SimTval;
static socklen_t _darV3SimServerLen, _darV3SimOptlen;
static Tx_Thread _darV3SimActualData;
static sem_t _darV3SimTimeSem;
static pthread_mutex_t _darV3SimStatsMutex;
static u_int32_t _darV3SimTickCount = 0;
static Stats _darV3SimSThread;

static void _darV3SimSigIntHandler(int32_t signum)
{
    _darV3SimExit = 1;
}

static void _darV3SimInitDarHeader(void)
{
    //DARV3 Header
    //signifies NPD Packet
    _darV3SimDummyData.darV3_header.hdr_top = DARV3SIM_DARHEAD_VERSION_DEFAULT;
    //bitshift over 4 places
    _darV3SimDummyData.darV3_header.hdr_top <<= DARV3SIM_DARHEAD_BITSHIFT;
    //number of 32-bit-words in header
    _darV3SimDummyData.darV3_header.hdr_top |= DARV3SIM_DARHEAD_OR_IN;
    //MACQ data
    _darV3SimDummyData.darV3_header.data_type = DARV3SIM_DARHEAD_DATATYPE;
    _darV3SimDummyData.darV3_header.config_count = DARV3SIM_DARHEAD_CONFIGCOUNT;
    /*Flags- represents the three values of RFT: 110.  packet contains...
     * R: 1- a relative time value
     * F: 1- NPD fragmentation is used for segments larger than the MTU
     * T: 0- Timestamp derived from free-running local time.
     * */
    _darV3SimDummyData.darV3_header.flags = DARV3SIM_DARHEAD_FLAGS;
    _darV3SimDummyData.darV3_header.seq_num = htons(DARV3SIM_SEQNUM_START);
    _darV3SimDummyData.darV3_header.DSID = htonl(DARV3_DSID_DEFAULT);
    _darV3SimDummyData.darV3_header.multicast_addr = inet_addr(
    DARV3SIM_MCADDR_DEFAULT);
}

static void _darV3SimInitSegHeader(void)
{
    // ACQ Header
    _darV3SimDummyData.seg_header.segment_length = htons(
    DARV3SIM_SEGHEADER_SIZE);
    _darV3SimDummyData.seg_header.error_code = DARV3SIM_SEGHEAD_ERRORCODE;
    /*segment header flags: 1 signifies 001,
     *  meaning this is a complete packet,
     *  and the CAL field is valid*/
    _darV3SimDummyData.seg_header.flags = DARV3SIM_SEGHEAD_FLAGS;
    _darV3SimDummyData.seg_header.SFID = DARV3SIM_SEGHEAD_SFID;
    _darV3SimDummyData.seg_header.cal_reserved_a = DARV3SIM_SEGHEAD_CALRES_A;
    _darV3SimDummyData.seg_header.cal_reserved_a <<=
    DARV3SIM_SEGHEAD_CALRES_BITSHIFT;
    _darV3SimDummyData.seg_header.reserved_b = DARV3SIM_SEGHEAD_CALRES_B;
}

static void _darV3SimInitProps(void)
{
    //PCM Stream _mfLength
    _darV3SimProps.frame_rate = DARV3SIM_PROPS_FRAMERATE;
    _darV3SimProps.num_sync_chars = DARV3SIM_PROPS_NUM_SYNCCHARS_DEFAULT;
    _darV3SimProps.sync_char_1 = DARV3SIM_PROPS_SYNCCHAR_DEFAULT;
    _darV3SimProps.sync_char_2 = DARV3SIM_PROPS_SYNCCHAR_DEFAULT;
    _darV3SimProps.bits_per_word = DARV3SIM_PROPS_BITSPWORD_DEFAULT;
    _darV3SimProps.words_per_min_f = DARV3SIM_PROPS_WORDSPMINF_DEFAULT;
    _darV3SimProps.min_fs_per_maj_f = DARV3SIM_PROPS_MINFSPMAJF;
}

static int32_t _darV3SimCmdLineOpts(int argc, char *argv[])
{
    int32_t k;
    char optstring[] = "a:d:c:h:s:b:w:m:n:u";
    opterr = 0;
    while ((k = getopt(argc, argv, optstring)) != -1)
    {
        switch (k)
        {
        case 'a':
            _darV3SimDummyData.darV3_header.multicast_addr = inet_addr(optarg);
            break;
        case 'd':
            _darV3SimDummyData.darV3_header.DSID = htonl(
                    strtoul(optarg, NULL, 0));
            break;
        case 'c':
            _darV3SimProps.sync_char_1 = strtoul(optarg, NULL, 16);
            break;
        case 's':
            _darV3SimProps.sync_char_2 = strtoul(optarg, NULL, 16);
            break;
        case 'h':
            _darV3SimProps.num_sync_chars = strtoul(optarg, NULL, 0);
            break;
        case 'w':
            _darV3SimProps.words_per_min_f = strtol(optarg, NULL, 0);
            break;
        case 'm':
            _darV3SimProps.min_fs_per_maj_f = strtol(optarg, NULL, 0);
            break;
        case 'n':
            _darV3SimProps.frame_rate = strtol(optarg, NULL, 0);
            break;
        case 'u':
            printf("Usage: darV3Sim\n"
                    "-a  <Multicast Address>,<%s>\n"
                    "-d  <DSID>,<0x%X>\n"
                    "-c  <sync character>,<0x%X>\n"
                    "-h  <number of sync characters>,<%i>\n"
                    "-s  <second sync character>,<0x%X> \n"
                    "-w  <words per minor frame>,<%i>\n"
                    "-m  <minor frames per major frame>,<%i> \n"
                    "-n  <major frame rate (per second)>,<%i> \n"
                    "-u  <Usage>\n",
            DARV3SIM_MCADDR_DEFAULT,
            DARV3_DSID_DEFAULT,
            DARV3SIM_PROPS_SYNCCHAR_DEFAULT,
            DARV3SIM_PROPS_NUM_SYNCCHARS_DEFAULT,
            DARV3SIM_PROPS_SYNCCHAR_DEFAULT,
            DARV3SIM_PROPS_WORDSPMINF_DEFAULT,
            DARV3SIM_PROPS_MINFSPMAJF,
            DARV3SIM_PROPS_FRAMERATE);
            return (-1);
        }
    }
    return 0;
}

static void _darV3SimOsTickCalc(void)
{
    _darV3SimPre = clock();
    do
    {
        _darV3SimCurrent = clock();
    } while (_darV3SimCurrent == _darV3SimPre);

    _darV3SimMsPerOsTick = _darV3SimCurrent - _darV3SimPre;

    _darV3SimOsTicksPerSec = (CLOCKS_PER_SEC) / _darV3SimMsPerOsTick;
    _darV3SimOsTickFraction = (CLOCKS_PER_SEC) % _darV3SimMsPerOsTick;

    _darV3SimFramesPerTick =
            (_darV3SimProps.frame_rate / _darV3SimOsTicksPerSec);
    _darV3SimFramesPerTickFrac = (_darV3SimProps.frame_rate
            % _darV3SimOsTicksPerSec);

    printf("CLOCKS_PER_SEC:              %d \n"
            "Ms Per Os Tick:             %d \n"
            "Os Ticks Per Sec:           %d \n"
            "OsTickFraction:             %d \n"
            "Frames Per Tick:            %d \n"
            "Frames Per Tick Frac:       %d \n",
    CLOCKS_PER_SEC, _darV3SimMsPerOsTick, _darV3SimOsTicksPerSec,
            _darV3SimOsTickFraction, _darV3SimFramesPerTick,
            _darV3SimFramesPerTickFrac);
}

static void _darV3SimPrintQuantities(void)
{
    //Print quantities
    printf("MC Address       = 0x%X\n"
            "DSID            = 0x%X\n"
            "syncChar1       = 0x%X\n"
            "syncChar2       = 0x%X \n"
            "bits_per_wrd    = %i\n"
            "wordsPerMinF    = %i\n"
            "minFsPerMajFs   = %i\n"
            "frame rate      = %i per second\n",
            ntohl(_darV3SimDummyData.darV3_header.multicast_addr),
            ntohl(_darV3SimDummyData.darV3_header.DSID),
            _darV3SimProps.sync_char_1, _darV3SimProps.sync_char_2,
            _darV3SimProps.bits_per_word, _darV3SimProps.words_per_min_f,
            _darV3SimProps.min_fs_per_maj_f, _darV3SimProps.frame_rate);
}

static void _darV3SimSizeCalc(int32_t* packetLength, int32_t* payloadLength)
{
    *payloadLength = ((_darV3SimProps.words_per_min_f
            * _darV3SimProps.bits_per_word) / 8);

    _darV3SimDummyData.seg_header.segment_length = htons(
    DARV3SIM_SEGHEADER_SIZE + *payloadLength);

    if ((_darV3SimProps.words_per_min_f % 2) == 1)
    {
        *payloadLength += sizeof(uint16_t);
        *packetLength += sizeof(uint16_t);
    }
    _darV3SimDummyData.darV3_header.packet_length = htons(
            (DARV3SIM_DARHEADER_SIZE + DARV3SIM_SEGHEADER_SIZE + *payloadLength)
                    / 4);
}

static int32_t _darV3SimDynAllocMemory(int32_t* payloadSize,
        int32_t* packetSize, int32_t* headersSize)
{
    *packetSize = *payloadSize + DARV3SIM_DARHEADER_SIZE
            + DARV3SIM_SEGHEADER_SIZE;
    *headersSize = DARV3SIM_DARHEADER_SIZE + DARV3SIM_SEGHEADER_SIZE;
    _darV3SimActualData.head_size = *headersSize;
    _darV3SimActualData.pack_size = *packetSize;
    _darV3SimActualData.pay_size = *payloadSize;
    _darV3SimEntirePacket = malloc(*packetSize);
    if (_darV3SimEntirePacket == NULL)
    {
        printf("[%s]malloc of _entirePacket failed.\n", __FUNCTION__);
        return (-1);
    }
    //Set all to 0 to eliminate junk data
    memset(_darV3SimEntirePacket, 0, *packetSize);
    return 0;
}

static void _darV3SimSetupDestAddr(void)
{
    memset(&_darV3SimDestAddr, 0, sizeof(_darV3SimDestAddr));
    _darV3SimDestAddr.sin_family = AF_INET;
    _darV3SimDestAddr.sin_addr.s_addr =
            _darV3SimDummyData.darV3_header.multicast_addr;
    _darV3SimDestAddr.sin_port = htons(DARV3SIM_DAR_PORT);
}

static void _darV3SimSetupSourceAddr(void)
{
    memset(&_darV3SimSourceAddr, 0, sizeof(_darV3SimSourceAddr)); //Clear structure
    _darV3SimSourceAddr.sin_family = AF_INET; //Set Address Type
    _darV3SimSourceAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    _darV3SimSourceAddr.sin_port = htons(0);
}

static int32_t _darV3SimSocketSetup(int32_t* sock)
{
    int32_t ttl = DARV3SIM_TTL_DEFAULT;
    int32_t so_reuseaddr = DARV3SIM_SO_REUSEADDR_DEFAULT;
    int32_t res;
    u_int32_t sendbuff = DARV3SIM_SENDBUFF_DEFAULT;
    char loopch = DARV3SIM_LOOPCH_DEFAULT;

    // Socket Options
    // create UDP socket
    if ((*sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("[%s] socket creation failed\n", __FUNCTION__);
        return (-1);
    }
    //TTL
    if (setsockopt(*sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0)
    {
        printf("[%s] Setting IP_MULTICAST_TTL error\n", __FUNCTION__);
        close(*sock);
        return (-1);
    }
    //Loopback
    if (setsockopt(*sock, IPPROTO_IP, IP_MULTICAST_LOOP, (char *) &loopch,
            sizeof(loopch)) < 0)
    {
        printf("[%s] Setting IP_MULTICAST_LOOP error\n", __FUNCTION__);
        close(*sock);
        return (-1);
    }
    else
    {
        printf("Loopback disabled\n");
    }
    //SO_REUSEADDR
    if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, (char *) &so_reuseaddr,
            sizeof(so_reuseaddr)) < 0)
    {
        printf("[%s] error SO_REUSEADDR%d\n", __FUNCTION__, errno);
        close(*sock);
        return (-1);
    }
    //SO_SNDBUF
    //Set buffer size
    printf("sets the send buffer to %d\n", sendbuff);
    if (setsockopt(*sock, SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff))
            < 0)
    {
        printf("[%s] Error setsockopt\n", __FUNCTION__);
        close(*sock);
        return (-1);
    }
    // Get buffer size
    _darV3SimOptlen = sizeof(sendbuff);
    res = getsockopt(*sock, SOL_SOCKET, SO_SNDBUF, &sendbuff, &_darV3SimOptlen);

    if (res == -1)
    {
        printf("[%s] Error getsockopt one\n", __FUNCTION__);
        close(*sock);
        return (-1);
    }
    else
        printf("buffer size = %u\n", sendbuff);

    // Bind
    if (bind(*sock, (struct sockaddr *) &_darV3SimSourceAddr,
            sizeof(_darV3SimSourceAddr)) < 0)
    {
        printf("[%s] SERVER bind failed.\n ", __FUNCTION__);
        close(*sock);
        return (-1);
    }

    _darV3SimServerLen = sizeof(_darV3SimSourceAddr); // Obtain address length
    // find picked port #
    if (getsockname(*sock, (struct sockaddr *) &_darV3SimSourceAddr,
            &_darV3SimServerLen) < 0)
    {
        printf("[%s] SERVER getsocketname failed.\n ", __FUNCTION__);
        close(*sock);
        return (-1);
    }
    return 0;
}

static void _darV3SimFillMinorFrame(u_int16_t *ptr, u_int32_t *ptrCounter)
{
    int32_t a;
    *ptr = htons(_darV3SimDummyData.seg_header.SFID);
    ptr++;
    for (a = 0; a < (_darV3SimProps.words_per_min_f
    - (1 + _darV3SimProps.num_sync_chars)); a++)
    {
        *ptr = htons((*ptrCounter)++);
        ptr++;
    }
    *ptr = htons(_darV3SimProps.sync_char_1);
    if (_darV3SimProps.num_sync_chars > 1)
    {
        ptr++;
        *ptr = htons(_darV3SimProps.sync_char_2);
    }
}

static void _darV3SimFillEntirePacket(u_int8_t *packet, u_int32_t *ptrCounter,
        int32_t headerSize)
{
    //fill with headers
    memcpy(packet, &_darV3SimDummyData, headerSize);
    packet += headerSize;
    //fill with payload
    _darV3SimFillMinorFrame((u_int16_t*) packet, ptrCounter);
}

static void *_darV3SimTimeThread(void *arg)
{
    if (pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, NULL) != 0)
    {
        fprintf( stderr, "[%s] setting pthread to cancelable failed. \n",
                __FUNCTION__);
    }
    else
    {
        if (pthread_setcanceltype( PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        {
            fprintf( stderr, "[%s] pthread cancel deferral failed. \n",
                    __FUNCTION__);
        }
    }
    while (1)
    {
        pthread_testcancel();
        usleep((1000000 / _darV3SimOsTicksPerSec));
        _darV3SimTickCount++;
        sem_post(&_darV3SimTimeSem);
    }
    return 0;
}

static void *_darV3SimTxThread(void *arg)
{
    //TX N packets (not input n, but calculated n)
    int32_t x = 0x0;
    u_int32_t counter = 0;
    uint32_t pkt_count = 0;
    uint32_t pkts_per_tick = 0;
    uint32_t minor_frame = 0;
    gettimeofday(&_darV3SimTval, NULL);
    _darV3SimSThread.start_sec = _darV3SimTval.tv_sec;
    _darV3SimSThread.start_usec = _darV3SimTval.tv_usec;
    if (pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, NULL) != 0)
    {
        fprintf( stderr, "[%s] setting pthread to cancelable failed. \n",
                __FUNCTION__);
    }
    else
    {
        if (pthread_setcanceltype( PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        {
            fprintf( stderr, "[%s] pthread cancel deferral failed. \n",
                    __FUNCTION__);
        }
    }
    while (1)
    {
        pthread_testcancel();
        sem_trywait(&_darV3SimTimeSem);
        if ((pkt_count >= _darV3SimProps.frame_rate * _darV3SimProps.min_fs_per_maj_f)
                && (_darV3SimTickCount < _darV3SimOsTicksPerSec))
        {
            continue;
        }
        else if (_darV3SimTickCount >= _darV3SimOsTicksPerSec)
        {
            pkt_count = 0;
            _darV3SimTickCount = 0;
        }
        if (pkt_count == 0)
        {
            pkts_per_tick = _darV3SimFramesPerTick + _darV3SimFramesPerTickFrac;
        }
        else
        {
            pkts_per_tick = _darV3SimFramesPerTick;
        }
        pkts_per_tick *= _darV3SimProps.min_fs_per_maj_f;

        for (x = 0; x < pkts_per_tick; x++)
        {

            _darV3SimDummyData.seg_header.SFID = (DARV3SIM_SEGHEAD_SFID
                    + minor_frame);
            if (minor_frame < (_darV3SimProps.min_fs_per_maj_f - 1))
            {
                minor_frame++;
            }
            else
            {
                minor_frame = 0;
            }

            //update timestamp and time delta
            gettimeofday(&_darV3SimTval, NULL);
            _darV3SimDummyData.darV3_header.time_stamp = htonl(
                    _darV3SimTval.tv_sec);

            _darV3SimDummyData.seg_header.time_delta = htonl(
                    _darV3SimTval.tv_usec * 1000);

            _darV3SimFillEntirePacket(_darV3SimEntirePacket, &counter,
                    _darV3SimActualData.head_size);

            //move dD into new void*
            if (sendto(_darV3SimUDPSocket, (void*) _darV3SimEntirePacket,
                    _darV3SimActualData.pack_size, 0,
                    (struct sockaddr *) &_darV3SimDestAddr,
                    sizeof(_darV3SimDestAddr)) < 0)
            {
                fprintf(stderr, "sendto() failed\n");
                continue;
            }
            pkt_count++;
            _darV3SimInc++;
            _darV3SimDummyData.darV3_header.seq_num = htons(_darV3SimInc);

        }
        //mutex
        pthread_mutex_lock(&_darV3SimStatsMutex);
        gettimeofday(&_darV3SimTval, NULL);
        _darV3SimSThread.time_total = _darV3SimTval.tv_sec
                - _darV3SimSThread.start_sec;
        _darV3SimSThread.time_total = (_darV3SimSThread.time_total * 1000000)
                + _darV3SimTval.tv_usec;
        _darV3SimSThread.time_total /= 1000;
        _darV3SimSThread.packet_num = _darV3SimInc;
        _darV3SimSThread.byte_num = _darV3SimActualData.pack_size
                * _darV3SimInc;

        pthread_mutex_unlock(&_darV3SimStatsMutex);
    }
    return 0;
}

static void *_darV3SimStatsThread(void *arg)
{
    if (pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, NULL) != 0)
    {
        printf("[%s] setting pthread to cancelable failed. \n", __FUNCTION__);
    }
    else
    {
        if (pthread_setcanceltype( PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        {
            printf("[%s] pthread cancel deferral failed. \n", __FUNCTION__);
        }
    }
    while (1)
    {
        pthread_testcancel();
        sleep(1);
        pthread_mutex_lock(&_darV3SimStatsMutex);

        //calculate stats
        _darV3SimSThread.packet_rate = (_darV3SimSThread.packet_num * 1000)
                / _darV3SimSThread.time_total;
        _darV3SimSThread.packet_mod = (_darV3SimSThread.packet_num * 1000)
                % _darV3SimSThread.time_total;
        _darV3SimSThread.byte_rate = (_darV3SimSThread.byte_num * 1000)
                / _darV3SimSThread.time_total;
        _darV3SimSThread.byte_mod = (_darV3SimSThread.byte_num * 1000)
                % _darV3SimSThread.time_total;

        if ((_darV3SimSThread.packet_mod != 0)
                && ((_darV3SimSThread.time_total / _darV3SimSThread.packet_mod)
                        <= 2))
        {
            _darV3SimSThread.packet_rate++;
        }
        if ((_darV3SimSThread.byte_mod != 0)
                && ((_darV3SimSThread.time_total / _darV3SimSThread.byte_mod)
                        <= 2))
        {
            _darV3SimSThread.byte_rate++;
        }

        //print stats
        fprintf(stderr,
                "Packets sent: %i, Packets per second: %i, Bytes sent: %i,"
                        " Bytes per second: %i\r", _darV3SimSThread.packet_num,
                _darV3SimSThread.packet_rate, _darV3SimSThread.byte_num,
                _darV3SimSThread.byte_rate);

        //release mutex
        pthread_mutex_unlock(&_darV3SimStatsMutex);

    }
    return 0;
}

int32_t main(int32_t argc, char *argv[])
{
    int32_t packetSize = 0;
    int32_t payloadSize = 0;
    int32_t headersSize = 0;
    int32_t rc, rd, re;
    pthread_t t1, t2, t3;
    pthread_attr_t attr; //CPW = 0;
    signal( SIGINT, _darV3SimSigIntHandler);
    sem_init(&_darV3SimTimeSem, 0, 0);

    //Initialize Buff
    _darV3SimInitDarHeader();
    _darV3SimInitSegHeader();
    _darV3SimInitProps();

    //Command Line Options
    if (_darV3SimCmdLineOpts(argc, argv) < 0)
    {
        goto DarV3Sim_Main_Failure_Early;
    }

    //Print to Make Sure Options went through
    _darV3SimPrintQuantities();

    // Calculate rate variables.  Make part of time thread
    _darV3SimOsTickCalc();

    // Calculate size variables
    _darV3SimSizeCalc(&packetSize, &payloadSize);

    // Dynamic Memory Allocatiion
    if (_darV3SimDynAllocMemory(&payloadSize, &packetSize, &headersSize) < 0)
    {
        printf("[%s] Error: Dynamic memory allocation", __FUNCTION__);
        goto DarV3Sim_Main_Failure_Early;
    }

    // set up destination address
    _darV3SimSetupDestAddr();

    // set up source address
    _darV3SimSetupSourceAddr();

    //Set Up UDP Socket
    if (_darV3SimSocketSetup(&_darV3SimUDPSocket) < 0)
    {
        printf("[%s] Error: Socket setup", __FUNCTION__);
        goto DarV3Sim_Main_Failure_Socket_Open;
    }

    //initiate mutex
    if (pthread_mutex_init(&_darV3SimStatsMutex, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        goto DarV3Sim_Main_Failure_Mutex;
    }
    //set threads to be joinable
    if (pthread_attr_init(&attr) != 0)
    {
        printf("\n join init failed\n");
        goto DarV3Sim_Main_Failure_Thread_Attr;
    }
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    /*Thread creation:
     * 1st arg: thread name.  2nd arg: thread attribute (default if NULL).   //|
     * 3rd arg: A function to run in the thread. The function must return void*
     *  and take a void* argument,which you may use however you see fit.
     * 4th arg: The void* that you want to start up the thread with.
     *   Pass NULL if you don't need it.
     */
    if ((rc = pthread_create(&t1, NULL, _darV3SimTimeThread, NULL)) < 0)
    {
        printf("\n ERROR creating thread 1");
        goto DarV3Sim_Main_Failure_Thread_1;
    }
    if ((rd = pthread_create(&t2, NULL, _darV3SimTxThread, NULL)) < 0)
    {
        printf("\n ERROR creating thread 2");
        goto DarV3Sim_Main_Failure_Thread_2;
    }
    if ((re = pthread_create(&t3, NULL, _darV3SimStatsThread, NULL)) < 0)
    {
        printf("\n ERROR creating thread 3");
        goto DarV3Sim_Main_Failure_Thread_3;
    }

    while (!_darV3SimExit)
    {
        sleep(1);
    }

    /* Deallocate the memory, clean up resources */

    pthread_cancel(t3);
    pthread_join(t3, NULL);
DarV3Sim_Main_Failure_Thread_3:
    pthread_cancel(t2);
    pthread_join(t2, NULL);

DarV3Sim_Main_Failure_Thread_2:
    pthread_cancel(t1);
    pthread_join(t1, NULL);

DarV3Sim_Main_Failure_Thread_1:
    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&_darV3SimStatsMutex);

DarV3Sim_Main_Failure_Mutex:

DarV3Sim_Main_Failure_Thread_Attr:
    close(_darV3SimUDPSocket);

DarV3Sim_Main_Failure_Socket_Open:
    free(_darV3SimEntirePacket);

DarV3Sim_Main_Failure_Early:
    sem_close(&_darV3SimTimeSem);

    printf("\nFREEEEEDOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOM \n");
    return 0;
}
