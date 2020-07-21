/*----------------------------------------------------------------------------
 * Copyright (c) <2018>, <Huawei Technologies Co., Ltd>
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *---------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------
 * Notice of Export Control Law
 * ===============================================
 * Huawei LiteOS may be subject to applicable export control laws and regulations, which might
 * include those applicable to Huawei LiteOS of U.S. and the country in which you are located.
 * Import, export and usage of Huawei LiteOS in any manner by you shall be in compliance with such
 * applicable export control laws and regulations.
 *---------------------------------------------------------------------------*/
/**
 *  DATE                AUTHOR      INSTRUCTION
 *  2020-05-15 10:18  zhangqianfu  The first version
 *
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <link_log.h>
#include <queue.h>
#include <oc_mqtt_al.h>
#include <oc_mqtt_profile.h>
#include <ota_flag.h>
#include <ota_https.h>
#include <cJSON.h>

const char* g_signature_public=
"-----BEGIN RSA PUBLIC KEY-----\r\n"
"MIIBCgKCAQEAz9u5vpDoov9DDrkWkwdWQnLjiYXO3RuwXmcSCu/N1Wrv55b3w/BJ\r\n"
"iXl7mTv1zWrU9gL+jdMXrxP6BK5nOh3wa8tiPGqPnM2tNCUVEt2dmDasroh8VVv1\r\n"
"9yOUiSlNGSZ+UrnuUlAzMLt0GJrCPHesapOQ7OkAQd2SNbQfv/vFmXzUcNUAZxr4\r\n"
"zJmSoZT9aXTO/RfShlsgrPtpz+8sejcRR5s4LKn5KsJwjqJ+sHmnEKlcGiciNGIx\r\n"
"Ajf2nFigo7QrZ+4o6kvweNA05Ptg29j/0JPr0WbyLsCWDVaAneelh8Sl3TPdZYOM\r\n"
"6iHE2k1sLBeP7X3lymWh3BZ9rU+xngi9lQIDAQAB\r\n"
"-----END RSA PUBLIC KEY-----\r\n";

//#define CN_SERVER_IPV4       "iot-mqtts.cn-north-4.myhuaweicloud.com"
#define CN_SERVER_IPV4                 "121.36.42.100"
#define CN_SERVER_PORT                 "1883"
#define CN_SECURITY_TYPE               EN_DTLS_AL_SECURITY_TYPE_NONE

#ifndef CONFIG_OCMQTTV5_DEMO_REPORTCYCLE
#define CONFIG_OCMQTTV5_DEMO_REPORTCYCLE   (10*1000)
#endif

#define CN_EP_DEVICEID        "xxxxxxxxxxxxx"
#define CN_EP_PASSWD          "xxxxx"
#define CN_BOOT_MODE            0
#define CN_LIFE_TIME            40
#define CN_OTA_SOTA_VERSION    "SOTAV1"
#define CN_OTA_FOTA_VERSION    "FOTAV1"
//if your command is very fast,please use a queue here--TODO
static queue_t                   *gRcvMsgQueue = NULL;   ///< this is used to cached the message
static oc_mqtt_profile_service_t  gDevicePropertyService;

//use this function to push all the message to the buffer
static int RcvMsgDealHook(oc_mqtt_profile_msgrcv_t *msg)
{
    int    ret = -1;
    char  *buf;
    int    buflen;
    int    datalen = 0;
    oc_mqtt_profile_msgrcv_t *demo_msg;

    ///< clone the message
    buflen = sizeof(oc_mqtt_profile_msgrcv_t) + msg->msg_len + 1;///< we copy with '\0' endings
    if(NULL != msg->request_id){
        buflen += strlen(msg->request_id) +1; ///< ending with '\0'
    }

    buf = osal_malloc(buflen);
    if(NULL != buf){
        demo_msg = (oc_mqtt_profile_msgrcv_t *)(uintptr_t)buf;
        buf += sizeof(oc_mqtt_profile_msgrcv_t);
        ///< copy the message and push it to the queue
        demo_msg->type = msg->type;
        if(NULL != msg->request_id){
            demo_msg->request_id = buf;
            datalen = strlen(msg->request_id);
            (void) memcpy(buf,msg->request_id,datalen);
            buf[datalen] = '\0';
            buf += (datalen+1);
        }
        else{
            demo_msg->request_id = NULL;
        }

        demo_msg->msg = buf;
        demo_msg->msg_len = msg->msg_len;
        datalen = msg->msg_len;
        (void) memcpy(buf,msg->msg,datalen);
        buf[datalen] = '\0';

        LINK_LOG_DEBUG("RCVMSG:type:%d reuqestID:%s payloadlen:%d payload:%s\n\r",\
                (int) demo_msg->type,demo_msg->request_id==NULL?"NULL":demo_msg->request_id,\
                demo_msg->msg_len,(char *)demo_msg->msg);
        ret = queue_push(gRcvMsgQueue,demo_msg,10);
        if(ret != 0){
            osal_free(demo_msg);
        }
    }

    return ret;
}

static int DealFirmupgradeEvent(cJSON *event)
{
    int ret = -1;
    cJSON *objParas;
    cJSON *objVersion;
    cJSON *objUrl;
    cJSON *objFileSize;
    cJSON *objAccesstoken;
    cJSON *objSign;
    ota_https_para_t otapara;

    objParas = cJSON_GetObjectItem(event, "paras");
    if(NULL != objParas){

        objVersion = cJSON_GetObjectItem(objParas, "version");
        objUrl = cJSON_GetObjectItem(objParas, "url");
        objFileSize = cJSON_GetObjectItem(objParas, "file_size");
        objAccesstoken = cJSON_GetObjectItem(objParas, "access_token");
        objSign = cJSON_GetObjectItem(objParas, "sign");

        if((NULL != objVersion) && (NULL != objUrl) && (NULL != objFileSize) && \
                (NULL != objAccesstoken) && (NULL != objSign)){
            otapara.authorize = cJSON_GetStringValue(objAccesstoken);
            otapara.url = cJSON_GetStringValue(objUrl);
            otapara.signature = cJSON_GetStringValue(objSign);
            otapara.file_size = objFileSize->valueint;
            otapara.version = cJSON_GetStringValue(objVersion);
            otapara.signature_public = g_signature_public;
            ///< here we do the firmware download
            ret =  ota_https_download(&otapara);
            if(ret != 0){
                OcMqttReportUpgradeProcessEvent(CN_EP_DEVICEID,6,otapara.version,-1);
                LINK_LOG_ERROR("DOWNLOADING ERR");
            }
            else {
                LINK_LOG_DEBUG("DOWNLOADING SUCCESS");
                osal_task_sleep(5*1000);
                osal_reboot();
            }
            ret = 0;
        }
    }
    return ret;
}


static int DealEvent(oc_mqtt_profile_msgrcv_t *msg)
{
    int ret = -1;
    cJSON *objRoot;
    cJSON *objServices;
    cJSON *objService;
    cJSON *objEventType;

    objRoot = cJSON_Parse(msg->msg);
    if(NULL == objRoot){
        goto EXIT_JSONFMT;
    }

    objServices = cJSON_GetObjectItem(objRoot,CN_OC_JSON_KEY_SERVICES);
    if((NULL == objServices) || (!cJSON_IsArray(objServices)))
    {
        goto EXIT_JSONSERVICEARRY;
    }

    cJSON_ArrayForEach(objService,objServices){
        objEventType = cJSON_GetObjectItem(objService,CN_OC_JSON_KEY_EVENTTYPE);
        if(NULL == objEventType){
            continue;
        }
        if(0 == strcmp(cJSON_GetStringValue(objEventType),CN_OC_MQTT_EVENTTYPE_VERSIONQUERY)){
            OcMqttReportVersionEvent(CN_EP_DEVICEID,CN_OTA_SOTA_VERSION, CN_OTA_FOTA_VERSION);
        }
        else if(0 == strcmp(cJSON_GetStringValue(objEventType),CN_OC_MQTT_EVENTTYPE_FWUPGRADE))
        {
            DealFirmupgradeEvent(objService);
        }
    }
    cJSON_Delete(objRoot);
    return 0;

EXIT_JSONSERVICEARRY:
    cJSON_Delete(objRoot);
EXIT_JSONFMT:
    return ret;
}

///< now we deal the message here
static int  DealRcvMsg(oc_mqtt_profile_msgrcv_t *demo_msg)
{
    static int value = 0;
    oc_mqtt_profile_cmdresp_t  cmdresp;
    oc_mqtt_profile_propertysetresp_t propertysetresp;
    oc_mqtt_profile_propertygetresp_t propertygetresp;

    switch(demo_msg->type){
        case EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_MSGDOWN:
            ///< add your own deal here
            break;
        case EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_COMMANDS:
            ///< add your own deal here
            ///< do the response
            cmdresp.paras = NULL;
            cmdresp.request_id = demo_msg->request_id;
            cmdresp.ret_code = 0;
            cmdresp.ret_name = NULL;
            (void)oc_mqtt_profile_cmdresp(NULL,&cmdresp);
            break;

        case EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_PROPERTYSET:
            ///< add your own deal here
            ///< do the response
            propertysetresp.request_id = demo_msg->request_id;
            propertysetresp.ret_code = 0;
            propertysetresp.ret_description = NULL;
            (void)oc_mqtt_profile_propertysetresp(NULL,&propertysetresp);
            break;

        case  EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_PROPERTYGET:
            ///< add your own deal here
            ///< do the response
            value  = (value+1)%100;
            gDevicePropertyService.service_property->key = "radioValue";
            gDevicePropertyService.service_property->value = &value;
            gDevicePropertyService.service_property->type = EN_OC_MQTT_PROFILE_VALUE_INT;
            propertygetresp.request_id = demo_msg->request_id;
            propertygetresp.services = &gDevicePropertyService;
            (void)oc_mqtt_profile_propertygetresp(NULL,&propertygetresp);
            break;
        case EN_OC_MQTT_PROFILE_MSG_TYPE_DOWN_EVENT:
            DealEvent(demo_msg);
            break;

        default:
            break;

    }
    return 0;
}

static int ReportProperty(void)
{
    int ret ;
    static int value = 0;

    gDevicePropertyService.service_property->key = "radioValue";
    gDevicePropertyService.service_property->value = &value;
    gDevicePropertyService.service_property->type = EN_OC_MQTT_PROFILE_VALUE_INT;
    value += 2;

    ret = oc_mqtt_profile_propertyreport(CN_EP_DEVICEID,&gDevicePropertyService);
    return ret;
}

static int RcvMsgDealTaskEntry( void *args)
{
    oc_mqtt_profile_msgrcv_t *demo_msg;
    while(1)
    {
        demo_msg = NULL;
        (void)queue_pop(gRcvMsgQueue,(void **)&demo_msg,(int)cn_osal_timeout_forever);
        if(NULL != demo_msg)
        {
            (void) DealRcvMsg(demo_msg);
            osal_free(demo_msg);
        }
    }
    return 0;
}

static void MqttStatusHookLog(en_oc_mqtt_log_t  logtype)
{

    if(logtype == en_oc_mqtt_log_connected){
        LINK_LOG_DEBUG("connected");
    }
    else{
        LINK_LOG_DEBUG("disconnected");
    }
    return;
}

static int ReportMsgTaskEntry(void *args)
{
    int ret = -1;
    oc_mqtt_profile_connect_t  connect_para;
    static ota_flag_t  otaflag;

    if(0 != ota_flag_get(EN_OTA_TYPE_FOTA,&otaflag)){
        LINK_LOG_ERROR("GET FOTA FLAG ERR AND SHOULD DO THE INITIALIZE");
        memset(&otaflag,0,sizeof(otaflag));
        otaflag.info.curstatus = EN_OTA_STATUS_IDLE;
        otaflag.info.upgrade_step = EN_OTA_UPGRADE_STEP_INIT;
        ret = ota_flag_save(EN_OTA_TYPE_FOTA,&otaflag);
        if(0 != ret){
            LINK_LOG_ERROR("FLAG SAVE FAILED AND WE SHOULD QUIT");
            return ret;
        }
    }

    (void) memset( &connect_para, 0, sizeof(connect_para));
    connect_para.boostrap =      CN_BOOT_MODE;
    connect_para.device_id =     CN_EP_DEVICEID;
    connect_para.device_passwd = CN_EP_PASSWD;
    connect_para.server_addr =   CN_SERVER_IPV4;
    connect_para.server_port =   CN_SERVER_PORT;
    connect_para.life_time =     CN_LIFE_TIME;
    connect_para.rcvfunc =       RcvMsgDealHook;
    connect_para.logfunc  = MqttStatusHookLog;
    connect_para.security.type = CN_SECURITY_TYPE;
    ret = oc_mqtt_profile_connect(&connect_para);
    if((ret != (int)en_oc_mqtt_err_ok)){
        LINK_LOG_ERROR("CONNECT ERR");
        return ret;
    }

    if(otaflag.info.curstatus == EN_OTA_STATUS_UPGRADED_SUCCESS){

        OcMqttReportUpgradeProcessEvent(CN_EP_DEVICEID,0,CN_OTA_FOTA_VERSION,100);
        otaflag.info.curstatus = EN_OTA_STATUS_IDLE;
        ota_flag_save(EN_OTA_TYPE_FOTA,&otaflag);
    }
    else if(otaflag.info.curstatus == EN_OTA_STATUS_UPGRADED_FAILED){
        OcMqttReportUpgradeProcessEvent(CN_EP_DEVICEID,0,CN_OTA_FOTA_VERSION,-1);
        otaflag.info.curstatus = EN_OTA_STATUS_IDLE;
        ota_flag_save(EN_OTA_TYPE_FOTA,&otaflag);
    }

    while(1){
        ReportProperty();
        osal_task_sleep(CONFIG_OCMQTTV5_DEMO_REPORTCYCLE);
    }
    return 0;
}

int standard_app_demo_main(void)
{
    static oc_mqtt_profile_kv_t  property;

    LINK_LOG_DEBUG("This is NUCLEOL496 MQTT OTA application--VERSION:%s",CN_OTA_FOTA_VERSION);
    gRcvMsgQueue = queue_create("queue_rcvmsg",2,1);
    ///< initialize the service
    property.nxt   = NULL;
    gDevicePropertyService.event_time = NULL;
    gDevicePropertyService.service_id = "DeviceStatus";
    gDevicePropertyService.service_property = &property;
    gDevicePropertyService.nxt = NULL;

    (void) osal_task_create("demo_reportmsg",ReportMsgTaskEntry,NULL,0x800,NULL,8);
    (void) osal_task_create("demo_rcvmsg",RcvMsgDealTaskEntry,NULL,0x1800,NULL,8);

    return 0;
}

static int VersionView(int argc, const char *argv[])
{
    LINK_LOG_DEBUG("S_VERSION:%s F_VERSION:%s",CN_OTA_SOTA_VERSION,CN_OTA_FOTA_VERSION);
    return 0;
}

#if CONFIG_LITEOS_ENABLE

#include <shell.h>
OSSHELL_EXPORT_CMD(VersionView,"version","version");
#endif

#if CONFIG_NOVAOS_ENABLE

#include <cmder.h>
static int nova_command( cmder_t *cer,int argc,char *argv[])
{

    return VersionView(argc, (const char **)argv);
}

CMDER_CMD_DEF ("version","version",nova_command);
#endif


