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
 *  2019-12-11 19:52  zhangqianfu  The first version  
 *
 */

#include <string.h>
#include <at.h>
#include <oc_mqtt_al.h>
#include <osal.h>
#include <stdlib.h>


#include <ec2x_oc.h>


#define CN_RCV_BUFLEN        1024
#define CN_EC2X_CMD_TIMEOUT  (15*1000)
#define CN_URC_DEACT         "\r\n+QIURC:"



typedef struct
{
    //initialized by the applicaiton
    fn_oc_mqtt_msg_deal  msg_dealer;
    void                *msg_dealer_args;
    int                  bsmode;
    int                  configured:1;
    int                  connected:1;
    osal_mutex_t         lock;    ///< this is the at lock here
} ec2x_cb_t;
static ec2x_cb_t   s_ec2x_cb;

//at command  with response
static bool_t ec2x_atcmd_response(const char *cmd,const char *index,char *buf, int len)
{
    int ret = 0;

    if(osal_mutex_lock(s_ec2x_cb.lock))
    {
        ret = at_command((unsigned char *)cmd,strlen(cmd),index,(char *)buf,len,CN_EC2X_CMD_TIMEOUT);
        osal_mutex_unlock(s_ec2x_cb.lock);
    }
    if(ret >= 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}


#define CN_EC2X_GETFIRMVER_CMD   "ATI\r\n"
int ec2x_getfirmver(ec2x_ver_t *ver)
{
    int ret = -1;
    char  resp[CN_EC2X_VERSION_LEN_MAX];
    char *str_start;
    char *str_end;
    int   cpylen;

    if(NULL  == ver)
    {
        return ret;
    }

    if(ec2x_atcmd_response(CN_EC2X_GETFIRMVER_CMD,"Revision:",resp,sizeof(resp)))
    {

        str_start = strstr(resp,":");
        if(NULL != str_start)
        {
            str_start++;
            str_end = strstr(str_start,"\r");
            if(NULL != str_end)
            {
                str_end--;
                cpylen = (int)(str_end - str_start);

                cpylen = cpylen > CN_EC2X_VERSION_LEN_MAX?CN_EC2X_VERSION_LEN_MAX:cpylen;

                memcpy(ver->ver,str_start,cpylen);
            }
            ret = 0;
        }
    }

    return ret;
}


//+QSPN: "CHINA MOBILE","CMCC","CMCC",0,"46000"
int ec2x_get_operator(char *buf, int len)
{
    int ret = -1;
    char resp[128];
    char *str;
    char *str_s;
    char *str_e;

    memset(resp,0,128);
    if (ec2x_atcmd_response("AT+QSPN\r\n","+QSPN:",resp,128))
    {
        str = strstr(resp,":");
        if(NULL != str)
        {
            str_s = str;
            str_s++;
            str_s++;
            str_s++;
            str_e = strstr(str,",");
            if(NULL != str_e)
            {
                str_e--;
                *str_e = '\0';
                strncpy(buf,str_s,len);
                ret = 0;
            }
        }
     }

    return ret;
}

//AT+QLTS=2
//+QLTS: "2019/12/12,10:49:42+32,0"
int ec2x_get_time(char *timebuffer)
{
    int ret = -1;
    char resp[128];
    char *str;
    int zone;

    char *str_s;
    char *str_e;
    char *str_z;


    memset(resp,0,128);
    if (ec2x_atcmd_response("AT+QLTS=2\r\n","+QLTS",resp,128))
    {
        str = strstr(resp,"\"");
        if(NULL == str)
        {
            goto EXIT_ERR;
        }
        str ++;
        str_s = str;


        str = strstr(str,"+");
        if(NULL == str)
        {
            goto EXIT_ERR;
        }
        str_e = str;

        str++;
        str_z = str;


        str = strstr(str,",");
        if(NULL == str)
        {
            goto EXIT_ERR;
        }

        *str = '\0';
        zone = atoi(str_z)/4;
        sprintf(str_e," GTM+%d",zone);

        strcpy(timebuffer,str_s);

        ret = 0;
    }
    return ret;

EXIT_ERR:
    return ret;

}

//+CSQ: 29,99
int ec2x_get_csq(int *csq)
{
    char resp[64];
    char *str_s;
    int ret = -1;


    memset(resp,0,64);
    if(ec2x_atcmd_response("AT+CSQ\r\n","+CSQ: ",resp,64))
    {
        str_s = strstr(resp,"+CSQ: ");
        if(NULL == str_s)
        {
            goto EXIT_ERR;
        }
        ret = sscanf(str_s,"+CSQ: %d,",csq);
        if(ret > 0)
        {
            ret = 0;
        }
        else
        {
            ret = -1;
        }
    }
    return ret;

 EXIT_ERR:
    return ret;
}



static char *find_nchar(char *str,char c,int n)
{
    char *ret = NULL;
    char *p = str;
    int count = 0;

    while(*p != '\0')
    {
        if(*p == c)
        {
            count ++;
            if(count == n)
            {
                ret = p;
                break;
            }
        }
        p++;
    }
    return ret;
}
//+QENG: "servingcell","NOCONN","LTE","TDD",460,00,2C5D704,367,38950,40,5,5,27B7,-78,-7,-50,22,49
int ec2x_get_rssi(int *rssi)
{
    int  ret = -1;
    char resp[128];
    char *str_s;
    char *str_e;

    memset(resp,0,128);

    if(ec2x_atcmd_response("AT+QENG=\"servingcell\"\r\n","+QENG:",resp,128))
    {
        str_s = find_nchar(resp,',',15);
        str_e = find_nchar(resp,',',16);
        if((NULL != str_s) && (NULL != str_e))
        {
            ret = sscanf(str_s,",%d,",rssi);
            if(ret > 0)
            {
                ret = 0;
            }
            else
            {
                ret = -1;
            }
        }
    }
    return ret;
}


#define CN_EC2X_OC_MSGINDEX  "\r\n\r\n+HWOCMQTTRECEIVED="
static int ec2x_oc_msg_deal(void *args,void *msg,size_t len)
{
    int   qos = 0;
    int   payload_len = 0;
    char *payload = NULL;

    mqtt_al_msgrcv_t  oc_msg;

    printf("recv:%s len:%d\r\n", (char *)msg, len);

    sscanf((char *)msg,CN_EC2X_OC_MSGINDEX"%d,%d",&qos,&payload_len);

    printf("RCVMSG:QOS:%d MSGLEN:%d\n\r",qos,payload_len);

    payload = osal_malloc(payload_len);

    if(NULL != payload)
    {
        hexstr2byte((char *)msg + strlen(CN_EC2X_OC_MSGINDEX),payload_len*2,payload);

        memset(&oc_msg,0,sizeof(oc_msg));
        oc_msg.qos = qos;
        oc_msg.msg.data = payload;
        oc_msg.msg.len = payload_len;

        if(NULL != s_ec2x_cb.msg_dealer)
        {
            s_ec2x_cb.msg_dealer(s_ec2x_cb.msg_dealer_args,&oc_msg);
        }

        osal_free(payload);

    }

    return 0;
}

static int ec2x_oc_deconfig(void)
{
    int ret = en_oc_mqtt_err_system;
    char cmd[256];
    char resp[64];

    memset(cmd,0,256);
    snprintf(cmd,256,"AT+HWOCMQTTDISCONNECT\r");
    if(ec2x_atcmd_response(cmd,"+DISCONNECTED ",resp,64))
    {
        if(NULL != strstr(resp,"+DISCONNECTED OK"))
        {
            ret = en_oc_mqtt_err_ok;
            s_ec2x_cb.configured = 0;
            s_ec2x_cb.connected = 0;
            s_ec2x_cb.msg_dealer = NULL;
            s_ec2x_cb.msg_dealer_args = NULL;
        }
        else
        {
            if(1 != sscanf(resp,"\r\n\r\n+DISCONNECTED ERR:%d",&ret))
            {
                ret = en_oc_mqtt_err_system;
            }
        }
    }
    else
    {
        ret = en_oc_mqtt_err_system;///< not response for the at command
    }
    return ret;
}

static int ec2x_oc_config(oc_mqtt_config_t *param)
{
    int ret = en_oc_mqtt_err_system;
    int bs_mode = 0;
    char cmd[256];
    char resp[64];

    if(param->boot_mode == en_oc_mqtt_mode_bs_static_nodeid_hmacsha256_notimecheck_json)
    {
        bs_mode = 1;
    }

    memset(cmd,0,256);
    snprintf(cmd,256,"AT+HWOCMQTTCONNECT=%d,%d,\"%s\",\"%s\",\"%s\",\"%s\"\r",\
            s_ec2x_cb.bsmode, param->lifetime, param->server_addr, param->server_port, param->id, param->pwd);
    if(ec2x_atcmd_response(cmd,"+CONNECTED ",resp,64))
    {
        if(NULL != strstr(resp,"+CONNECTED OK"))
        {
            s_ec2x_cb.configured = 1;
            s_ec2x_cb.bsmode = bs_mode;
            s_ec2x_cb.msg_dealer = param->msg_deal;
            s_ec2x_cb.msg_dealer_args = param->msg_deal_arg;
            ret = en_oc_mqtt_err_ok;
        }
        else
        {
            if(1 != sscanf(resp,"\r\n\r\n+CONNECTED ERR:%d",&ret))
            {
                ret = en_oc_mqtt_err_system;
            }
        }
    }
    else
    {
        ret = en_oc_mqtt_err_system;///< not response for the at command
    }

    return ret;
}




#define CN_EC2X_SEND_FMT  "AT+HWOCMQTTSEND=%d,%d,"

static int ec2x_oc_publish(char *topic,uint8_t *msg,int len,int qos)
{
    int ret = en_oc_mqtt_err_system;
    char *cmd = NULL;
    char resp[64];
    int cmdlen;

    if(NULL != topic) ///< NOW WE DON'T SUPPORT THE TOPIC MODE:--todo
    {
        ret = en_oc_mqtt_err_parafmt;
    }
    else if((NULL == msg) || (0 == len ) ||(qos >= en_mqtt_al_qos_err))
    {
        ret = en_oc_mqtt_err_parafmt;
    }
    else
    {
        cmdlen = strlen(CN_EC2X_SEND_FMT) + 8 + len*2;
        cmd = osal_malloc(cmdlen);
        if(NULL != cmd)
        {
            memset(cmd,0,cmdlen);
            cmdlen = snprintf(cmd,cmdlen,CN_EC2X_SEND_FMT,qos,len);
            byte2hexstr(msg,len,&cmd[cmdlen]);
            cmdlen += len*2;
            cmd[cmdlen] = '\r';
            if(ec2x_atcmd_response(cmd,"+SEND ",resp,64))
            {
                if(NULL != strstr(resp,"+SEND OK"))
                {
                    ret = en_oc_mqtt_err_ok;
                }
                else
                {
                    if(1 != sscanf(resp,"\r\n\r\n+SEND ERR:%d",&ret))
                    {
                        ret = en_oc_mqtt_err_system;
                    }
                }
            }
            else
            {
                ret = en_oc_mqtt_err_system;///< not response for the at command
            }
            osal_free(cmd);

        }
        else
        {
            ret = en_oc_mqtt_err_sysmem;
        }
    }

    return ret;
}

static const oc_mqtt_t s_ec2x_oc =
{
    .name = "ec2x_oc",
    .op = {
        .config = ec2x_oc_config,
        .deconfig = ec2x_oc_deconfig,
        .publish =  ec2x_oc_publish,
        .subscribe = NULL,
    },
};

int ec2x_oc_init(void)
{
    int ret = -1;
    osal_mutex_create(&s_ec2x_cb.lock);

    at_oobregister("ec2x_ocmsg",CN_EC2X_OC_MSGINDEX,strlen(CN_EC2X_OC_MSGINDEX),ec2x_oc_msg_deal,NULL);

    ret = oc_mqtt_register(&s_ec2x_oc);
    return ret;
}

//{[89445007031960706710,01,00],[89852019919070118516,02,01],[898602B4151880002302,02,00],[89860317422046189762,02,00]}
#define CN_GETICCIDLIST_CMD   "AT+HWICCIDLIST\r\n"
int ec2x_geticcidtab(iccid_tab_t  *tab)
{
    int ret = -1;
    char  resp[256];
    char *str;
    int i = 0;

    if(NULL  == tab)
    {
        return ret;
    }


    if(ec2x_atcmd_response(CN_GETICCIDLIST_CMD,"+HWICCIDLIST ",resp,sizeof(resp)))
    {
        str = strstr(resp,"OK:");
        if(NULL != str)
        {
            ret = 0;
            ///< decode the num
            str += strlen("OK:");
            tab->num = *str - '0';
            ///< decode at most num times
            for(i = 0;i<tab->num;i++)
            {
                str = strstr(str,"[");
                if(NULL != str)
                {
                    sscanf(str,"[%20s,%d,%d]",(char *)&tab->iccid[i].id,&tab->iccid[i].type,&tab->iccid[i].status);
                    str = strstr(str,"]");
                }
                else
                {
                    tab->num = i;
                    break;
                }
            }
        }
    }
    return ret;
}


#define CN_ICCID_ENABLE_CMD_FMT  "AT+HWICCIDENABLE=%s\r\n"
int ec2x_eniccid(iccid_t *iccid)
{
    int ret = -1;
    char  cmd[64];
    char  resp[64];

    if((NULL  == iccid) || (iccid->status == 1))
    {
        return ret;
    }


    memset(cmd,0,sizeof(cmd));
    snprintf(cmd,64,CN_ICCID_ENABLE_CMD_FMT,iccid->id);

    if(ec2x_atcmd_response(cmd,"+HWICCIDENABLE OK",resp,sizeof(resp)))
    {
        ret = 0;
    }
    return ret;
}

//+HWEID OK:89086001202200101019000000390817
#define CN_ICCID_GETEID_CMD_FMT  "AT+HWEID\r\n"
int ec2x_geteid(eid_t *eid)
{

    int ret = -1;
    char  resp[64];
    char *str;

    if(NULL  == eid)
    {
        return ret;
    }
    if(ec2x_atcmd_response(CN_ICCID_GETEID_CMD_FMT,"+HWEID OK:",resp,sizeof(resp)))
    {
        str = strstr(resp,"+HWEID OK:");
        if(NULL != str)
        {
            sscanf(str,"+HWEID OK:%32s",eid->id);
            ret = 0;
        }
    }
    return ret;
}

///< AT+HWOCMQTTVERSION



//+HWOCMQTTVERSION: V1.3.0 AT 11:41:19 ON Dec 26 2019
#define CN_EC2X_GETMQTTVERSION_CMD  "AT+HWOCMQTTVERSION\r\n"
int ec2x_getmqttversion(ec2x_ver_t *ver)
{
    int ret = -1;
    char  resp[CN_EC2X_VERSION_LEN_MAX];
    char *str_start;
    char *str_end;
    int   cpylen;

    if(NULL  == ver)
    {
        return ret;
    }

    if(ec2x_atcmd_response(CN_EC2X_GETMQTTVERSION_CMD,"+HWOCMQTTVERSION:",resp,sizeof(resp)))
    {
        str_start = strstr(resp,":");
        if(NULL != str_start)
        {
            str_start++;
            str_end = strstr(str_start,"\r");
            if(NULL != str_end)
            {
                str_end--;
                cpylen = (int)(str_end - str_start);
                memcpy(ver->ver,str_start,cpylen);
            }
            ret = 0;
        }
    }

    return ret;
}


int ec2x_echoset(int mode)
{
    int ret = -1;
    char  cmd[64];


    snprintf(cmd,64,"ATE%d\r\n",mode);

    if(ec2x_atcmd_response(cmd,"OK",NULL,0))
    {
        ret = 0;
    }

    return ret;
}



#include <shell.h>
static iccid_tab_t s_iccid_tab;

static int shell_iccidlist(int argc,const char *argv[])
{
    int ret = -1;
    int i = 0;

    memset(&s_iccid_tab,0,sizeof(s_iccid_tab));
    ret = ec2x_geticcidtab(&s_iccid_tab);
    if(0 == ret)
    {
        printf("GET ICCID OK:NUM:%d\n\r",s_iccid_tab.num);
        for(i = 0;i<s_iccid_tab.num;i++)
        {
            printf("NUM:%d:TYPE:%d STATUS:%d ID:%s\n\r",i,\
                    s_iccid_tab.iccid[i].type,s_iccid_tab.iccid[i].status,s_iccid_tab.iccid[i].id);
        }
    }
    else
    {
        printf("GET ICCID ERR\n\r");
    }

    return ret;
}

OSSHELL_EXPORT_CMD(shell_iccidlist,"iccidlist","iccidlist");


static int shell_iccidenable(int argc,const char *argv[])
{
    int ret = -1;
    int i = 0;

    if(argc != 2)
    {
        return ret;
    }

    i = strtol(argv[1],NULL,0);

    if(i >= s_iccid_tab.num)
    {
        return ret;
    }

    ret = ec2x_eniccid(&s_iccid_tab.iccid[i]);

    return ret;
}

OSSHELL_EXPORT_CMD(shell_iccidenable,"icciden","icciden number");

static int shell_eid(int argc,const char *argv[])
{
    int ret = -1;
    eid_t eid;


    memset(&eid,0,sizeof(eid));
    ret = ec2x_geteid(&eid);

    if(0 == ret)
    {
        printf("EID OK:%s\n\r",eid.id);
    }
    else
    {
        printf("EID ERR \n\r");
    }

    return ret;
}

OSSHELL_EXPORT_CMD(shell_eid,"eid","eid");



