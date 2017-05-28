#include <stdio.h>
#include <string.h>
#include <c_types.h>

#include <esp8266/pin_mux_register.h>
#include <esp8266/eagle_soc.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <lwip/lwip/ip_addr.h>
#include <espconn.h>

#define MAXTIMINGS 10000
#define BREAKTIME 20
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
static volatile os_timer_t some_timer;

static float lastTemp, lastHum;

static char hwaddr[6];

static void ICACHE_FLASH_ATTR
at_tcpclient_sent_cb(void *arg) {
    printf("sent callback\n");
    struct espconn *pespconn = (struct espconn *)arg;
    espconn_disconnect(pespconn);
}

static void ICACHE_FLASH_ATTR
at_tcpclient_discon_cb(void *arg) {
    struct espconn *pespconn = (struct espconn *)arg;
    os_free(pespconn->proto.tcp);

    os_free(pespconn);
    printf("disconnect callback\n");

}
static void ICACHE_FLASH_ATTR
at_tcpclient_connect_cb(void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;

    printf("tcp client connect\r\n");
    printf("pespconn %p\r\n", pespconn);

    char payload[128];

    espconn_regist_sentcb(pespconn, at_tcpclient_sent_cb);
    espconn_regist_disconcb(pespconn, at_tcpclient_discon_cb);
    os_sprintf(payload, MACSTR ",%d,%d\n", MAC2STR(hwaddr), (int)(lastTemp*100), (int)(lastHum*100));
    printf(payload);
    //sent?!
    espconn_sent(pespconn, payload, strlen(payload));
}

ICACHE_FLASH_ATTR void  readDHT(void *arg);

int globalTemp;

static void ICACHE_FLASH_ATTR
sendReading(float t, float h)
{
//    struct espconn *pCon = (struct espconn *)os_zalloc(sizeof(struct espconn));
//    if (pCon == NULL)
//    {
//        printf("CONNECT FAIL\r\n");
//        return;
//    }
//    pCon->type = ESPCONN_TCP;
//    pCon->state = ESPCONN_NONE;
//
//    uint32_t ip = ipaddr_addr("192.168.1.200");
//
//    pCon->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
//    pCon->proto.tcp->local_port = espconn_port();
//    pCon->proto.tcp->remote_port = 1337;
//
//    //gad vide hvorfor denne ikke bare sættes ligesom alt det andet?
//    os_memcpy(pCon->proto.tcp->remote_ip, &ip, 4);
//
//    //u need some functions
//    espconn_regist_connectcb(pCon, at_tcpclient_connect_cb);
//    //kan den undværes?
//    //espconn_regist_reconcb(pCon, at_tcpclient_recon_cb);
//    espconn_connect(pCon);

    printf("readDHT %p Temp =  %d , Hum = %d \n", readDHT, (int)(t*100), (int)(h*100));

    globalTemp = (int)(t*100);
//    //we will use these in the connect callback..
//    lastTemp = t;
//    lastHum = h;
}

#define DHT_PIN 2
#define DHT_MAXCOUNT 1000
#define BREAKTIME 20


ICACHE_FLASH_ATTR void  readDHT(void *arg)
{
    int counter = 0;
    int laststate = 1;
    int i = 0;
    int j = 0;
    int checksum = 0;
    //int bitidx = 0;
    //int bits[250];

    int data[100];

    data[0] = data[1] = data[2] = data[3] = data[4] = 0;

    // Wake up device, 250ms of high
    GPIO_OUTPUT_SET(DHT_PIN, 1);
    os_delay_us(250 * 1000);

    // Hold low for 20ms
    GPIO_OUTPUT_SET(DHT_PIN, 0);
    os_delay_us(20 * 1000);

    //disable interrupts, start of critical section
    //ets_intr_lock();

    // High for 40ms
    GPIO_OUTPUT_SET(DHT_PIN, 1);
    os_delay_us(40);

    GPIO_DIS_OUTPUT(2);
    PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO2_U);

    // wait for pin to drop?
    while (GPIO_INPUT_GET(DHT_PIN) == 1 && i < DHT_MAXCOUNT) {
        os_delay_us(1);
        i++;
    }
    if (i == DHT_MAXCOUNT)
        goto fail;

    // read data!
    for (i = 0; i < MAXTIMINGS; i++) {
        // Count high time (in approx us)
        counter = 0;
        while ( GPIO_INPUT_GET(DHT_PIN) == laststate) {
            counter++;
            os_delay_us(1);
            if (counter == DHT_MAXCOUNT)
                break;
        }
        laststate = GPIO_INPUT_GET(DHT_PIN);
        if (counter == 1000) break;

        //bits[bitidx++] = counter;
        if ((i > 3) && (i % 2 == 0)) {
            // shove each bit into the storage bytes
            data[j/8] <<= 1;
            if (counter > BREAKTIME)
                data[j / 8] |= 1;
            j++;
        }
    }

    //Re-enable interrupts, end of critical section
    //ets_intr_unlock();

    if (j < 40) {
        printf("Got too few bits: %d should be at least 40\r\n", j);
        goto fail;
    }

    checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    if (data[4] != checksum) {
        printf("DHT: %02x %02x %02x %02x [%02x] CS: %02x\r\n",
                  data[0], data[1], data[2], data[3], data[4], checksum);
        printf("Checksum was incorrect after %d bits. Expected %d but got %d\r\n",
                  j, data[4], checksum);
        goto fail;
    }

/*
  for (i=3; i<bitidx; i+=2) {
  printf("bit %d: %d\n", i-3, bits[i]);
  printf("bit %d: %d (%d)\n", i-2, bits[i+1], bits[i+1] > BREAKTIME);
  }
  printf("Data (%d): 0x%x 0x%x 0x%x 0x%x 0x%x\n", j, data[0], data[1], data[2], data[3], data[4]);
*/
    float temp_p, hum_p;

    hum_p = data[0] * 256 + data[1];
    hum_p /= 10;

    temp_p = (data[2] & 0x7F)* 256 + data[3];
    temp_p /= 10.0;
    if (data[2] & 0x80)
        temp_p *= -1;

    sendReading(temp_p, hum_p);

    return;
fail:
    //Re-enable interrupts, end of critical section
    //ets_intr_unlock();

    printf("Failed to get reading, dying\r\n");
    return;
}
#if 0
void ICACHE_FLASH_ATTR user_init(void)
{
    printf("\r\nGet this sucker going!\r\n");

    //Set GPIO2 to output mode
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
    PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO2_U);

    wifi_get_macaddr(0, hwaddr);

    os_timer_disarm(&some_timer);

    //Setup timer
    os_timer_setfn(&some_timer, (os_timer_func_t *)readDHT, NULL);

    //Arm the timer
    //&some_timer is the pointer
    //1000 is the fire time in ms
    //0 for once and 1 for repeating
    os_timer_arm(&some_timer, 4000, 1);

}
#endif
