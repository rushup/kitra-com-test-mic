﻿#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <iostream>
#include <fstream>
#include "kitra_input.h"
#include "kitra_output.h"
#include "kitra_packet_generator.h"
#include "kitra_small_utility.h"
#include "serial.h"
#include <time.h>
#include <errno.h>
#include <string.h>

using namespace std;

serial kitra_serial;
char response_obj[500];
uint32_t packet_size;
uint32_t optional_mask;
static uint32_t tick = 0;
char buffer[1024];
int buffer_len;


void kitra_platform_send(char* buffer, uint32_t length)
{
    /*Print through serial*/
    kitra_serial.serialPuts(buffer);
    k_unlock_tx();
}

std::string wait_for_packet()
{
    std::string cmd;
    bool begin = false;
    while(1)
    {
        int n = kitra_serial.serialDataAvail();
        for(int i=0; i<n;i++)
        {
            int ris = kitra_serial.serialGetchar();
            if(ris >= 0 && ris <= 127)
            {
                if(cmd.length() > 1000) //something went wrong
                    cmd.clear();

                if(ris == '$')
                {
                    begin = true;
                    cmd.clear();
                }
                if(begin == true)
                {
                    cmd+=(char)ris;
                }
                if(ris == '\n')
                {
                    begin = false;
                    return cmd;
                }
            }
        }
    }
}

#define PACKET_LENGHT       51      /* $KITRA,683,32,################################*cc\r\n */
#define PACKET_LENGHT_S     83      /* $KITRA,683,64,################################@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@*cc\r\n */
#define MIC_FIX_HEADER      14      /* $KITRA,683,32, */
#define MIC_FIX_DATA        32      /* ################################ */
#define MIC_FIX_DATA_S      64      /* ################################@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ */
#define MIC_FIX_END         5       /* *cc\r\n */
#define INPUT_DATA_BUFF     256

bool wait_packet_complete(char* outBuff)
{
    static uint32_t incoming_data_lenght = 0;
    static int start_mic_data = 0;
    static bool begin = false;
    static char incoming_data[INPUT_DATA_BUFF];
    static char* pch;

    int data_available = kitra_serial.serialDataAvail();

    /* 1. Wait minimum data for decode ID message */
    if (data_available > 0)
    {
        if (incoming_data_lenght >= (INPUT_DATA_BUFF - 1))
        {
            incoming_data_lenght = 0;
            memset(incoming_data, 0, sizeof(incoming_data));
            return false;
        }

        incoming_data[incoming_data_lenght] = kitra_serial.serialGetchar();
        incoming_data_lenght++;
    }
    else
    {
        return false;
    }

    /* 2. check if ID is Microfone stream "$KITRA,683,32," */
    if (begin == false)
    {
#if STEREO == 1
        pch = (char*)memmem(incoming_data, sizeof(incoming_data), "$KITRA,683,64,", MIC_FIX_HEADER);
#else
        pch = (char*)memmem(incoming_data, sizeof(incoming_data), "$KITRA,683,32,", MIC_FIX_HEADER);
#endif
        if (pch != 0)
        {
            begin = true;
            start_mic_data =  (pch + MIC_FIX_HEADER) - incoming_data;
        }
        else
        {
            return false;
        }
    }
#if STEREO == 1
    if (incoming_data_lenght < (start_mic_data + MIC_FIX_DATA_S + MIC_FIX_END))
    {
        return false;
    }
#else
    if (incoming_data_lenght < (start_mic_data + MIC_FIX_DATA + MIC_FIX_END))
    {
        return false;
    }
#endif

    /* 3. Extract RAW PCM audio */
    memcpy(outBuff, (pch + MIC_FIX_HEADER), MIC_FIX_DATA_S);

    /* 4. restart process */
    pch = NULL;
    memset(incoming_data, 0, sizeof(incoming_data));
    incoming_data_lenght = 0;
    begin = false;
    return true;
}


void wait_ack(const char* str, uint32_t ref_id, bool blocking)
{
    while(1)
    {
        if(k_parse_packet_safe(wait_for_packet().c_str(),(void*)response_obj,&packet_size, &optional_mask) == PARSE_OK)
        {
            k_output_ack* ack = (k_output_ack*) response_obj;
            k_output_nack* nack = (k_output_nack*) response_obj;
            if(ack->id == K_OUTPUT_ACK && ack->ref_id == ref_id)
            {
                printf("%s",str);
                break;
            }
            else if(!blocking || (nack->id == K_OUTPUT_NACK && nack->ref_id == ref_id))
            {
                printf("ERROR %s",str);
                break;
            }
        }
    }
}

void enable_leds()
{
    k_input_ldrgb_enable_disable msg1 = {K_INPUT_LDRGB_ENABLE_DISABLE,0,1};
    k_send_packet(&msg1,0);
    wait_ack("LED ENABLE ACKED\n",K_INPUT_LDRGB_ENABLE_DISABLE,true);
}

void change_baud_rate(uint32_t baud)
{
    k_input_change_baud_rate msg1 = {K_INPUT_CHANGE_BAUD_RATE,baud};
    k_send_packet(&msg1,0);
}

void leds(uint32_t color)
{
    k_input_ldrgb_set msg2 = {K_INPUT_LDRGB_SET,0,color,50,1};
    k_send_packet(&msg2,0);
    wait_ack("LED SET ACKED\n",K_INPUT_LDRGB_SET,true);
}

void enable_mic_stream()
{
#if STEREO == 1
    k_input_mic_enable_disable msg1 = {K_INPUT_MIC_ENABLE_DISABLE,2,0x12,1,2000,0,0};      /* Enable Stream with output Stereo from Mic 3 and 4 */
#else
    k_input_mic_enable_disable msg1 = {K_INPUT_MIC_ENABLE_DISABLE,2,0x10,1,2000,0,0};
#endif
    k_send_packet(&msg1,0);
}

void enable_source_localization()
{
    k_input_mic_enable_disable msg1 = {K_INPUT_MIC_ENABLE_DISABLE,1,0x12,1,2000,0,0};      /* Enable Localization with output stereo from Mic 1 and 2 */
    k_send_packet(&msg1,0);
    wait_ack("MIC ENABLE ACKED\n",K_INPUT_MIC_ENABLE_DISABLE,false);
}

void disable_mic()
{
    k_input_mic_enable_disable msg1 = {K_INPUT_MIC_ENABLE_DISABLE,0,0x12,0,0,0,0};
    k_send_packet(&msg1,0);
    usleep(20000);
    if (kitra_serial.serialDataAvail() > 0)
        kitra_serial.serialFlush();
}

void enable_beam_forming(uint8_t mic_1, uint8_t mic_2)
{
    uint8_t mic_12 = ((mic_1 & 0x0F) << 4) | (mic_2 & 0x0F);
    k_input_mic_enable_disable msg1 = {K_INPUT_MIC_ENABLE_DISABLE,3,mic_12,1,2000,3,0};      /* Enable BeamForming on Mic 1-2 */
    k_send_packet(&msg1,0);
    wait_ack("MIC ENABLE ACKED\n",K_INPUT_MIC_ENABLE_DISABLE,false);
}

void soft_reset()
{
    k_input_kitra_reset msg1 = {K_INPUT_KITRA_RESET,1};
    k_send_packet(&msg1,0);
    wait_ack("RESET ACKED\n",K_INPUT_KITRA_RESET,true);
}

/* Audio RAM Buffer */
#define FREQ_SAMPLE         16000  /* Hz from MIC */
#define SAMPLE_DURATION     4      /* Seconds */
#define SAMPLE_BIT          16     /* Single sample Bit (8 - 16 - 24 - 32) */
#define FRAME_LENGHT        MIC_FIX_DATA /* Number of byte in the received stream */

char audio_RAM_Buff[(SAMPLE_BIT / 8) * FREQ_SAMPLE * SAMPLE_DURATION];
uint32_t audio_frame_count = 0;

int main (int argc, char **argv)
{
    char audio_tmp[FRAME_LENGHT + FRAME_LENGHT]; /* Max Stereo */
    ofstream audioFile_1;
#if STEREO == 1
    ofstream audioFile_2;
#endif

    memset(audio_RAM_Buff, 0 , sizeof(audio_RAM_Buff));
    memset(audio_tmp, 0 , sizeof(audio_tmp));

    if(kitra_serial.serialOpen("/dev/ttySAC3", 115200) != -1)
    {
        change_baud_rate(1000000);
        usleep(50000);
        kitra_serial.serialClose();
        usleep(80000);
    }

    if(kitra_serial.serialOpen("/dev/ttySAC3", 1000000) != -1)
    {
        srand(time(NULL));

        printf("SERIAL FOUND\n");

        disable_mic();

        soft_reset();

        enable_leds();

        leds(0x000033);

        /* Wait 2 sec */
        sleep(2);

        leds(0x330000);

        if (kitra_serial.serialDataAvail() > 0)
        {
            kitra_serial.serialFlush();
        }

        enable_mic_stream();

#if ENABLE_BEAMFORMING
        enable_beam_forming(0x01, 0x02);
#endif

    }

    ((k_output_mic_sample_notification*)response_obj)->data = (char*) malloc(64);

#if STORE_IN_RAM == 1 /* Store in RAM all sample to reach SAMPLE_DURATION, and write in to the file at the end */
    while(1)
    {
        if (wait_packet_complete(audio_tmp) == true)
        {
            if (audio_frame_count < ((FREQ_SAMPLE * SAMPLE_DURATION) / (FRAME_LENGHT / 2)))
            {
                /* Copy new data */
                memcpy((char*)&audio_RAM_Buff[audio_frame_count * FRAME_LENGHT], audio_tmp, FRAME_LENGHT);
                memset(audio_tmp, 0, sizeof(audio_tmp));
                audio_frame_count++;
            }
            else
            {
                /* Create and open file */
                audioFile.open("pcm_audio_captured.raw", ios::out | ios::binary);
                if (audioFile.is_open())
                {
                    /* Save to file */
                    audioFile.write(audio_RAM_Buff, sizeof(audio_RAM_Buff));
                    /* Close */
                    audioFile.close();
                    /* Stop Mic */
                    disable_mic();
                    /* Change LED */
                    leds(0x220000);
                    /* End */
                    return 1;
                }

            }

        }
    }
#else   /* Little buffer in RAM and write to file each frame received until reach the second SAMPLE_DURATION */

    /* Create and open file */

    audioFile_1.open("pcm_audio_1.raw", ios::out | ios::binary);
#if STEREO == 1
    audioFile_2.open("pcm_audio_2.raw", ios::out | ios::binary);
#endif

    printf("START RECORDING\r\n");
    while(1)
    {
        if (wait_packet_complete(audio_tmp) == true)
        {
            if (audio_frame_count < ((FREQ_SAMPLE * SAMPLE_DURATION) / (FRAME_LENGHT / 2)))
            {
                /* Write data to file */
                audioFile_1.write(audio_tmp, FRAME_LENGHT);
#if STEREO == 1
                audioFile_2.write(&audio_tmp[FRAME_LENGHT], FRAME_LENGHT);
#endif
                memset(audio_tmp, 0, sizeof(audio_tmp));
                audio_frame_count++;
            }
            else
            {
                /* Close */
                audioFile_1.close();
#if STEREO == 1
                audioFile_2.close();
#endif
                printf("STOP RECORDING\r\n");
                /* Stop Mic */
                disable_mic();
                /* Wait mic to disable */
                usleep(1000);
                /* Clear serial trash */
                kitra_serial.serialFlush();
                /* Change LED */
                leds(0x003300);
                /* Wait 2 sec */
                sleep(2);
                leds(0x000000);
                /* End */
                return 1;
            }
        }
    }
#endif

    return 0;
}
