#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"


#include "esp_system.h" 
#include "esp_wifi.h" 

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "mqtt_client.h"


//Define del Wifi
#define EXAMPLE_ESP_WIFI_SSID "JCPA 8614"//"Claro_2.4G_0939" //"Fam Martinez"
#define EXAMPLE_ESP_WIFI_PASS "6W99*67g"//"sTy3utbu5a" //"mfaq7532" //sTy3utbu5a
#define EXAMPLE_ESP_MAXIMUM_RETRY 10
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1


#define Broker_Uri "mqtt://broker.emqx.io:1883"
#define TOPIC_1 "Adrian/state-machine"

#define TOGGLE_LAMPARA_500_ms 2
#define TOGGLE_LAMPARA_250_ms 1


//define de los gpios
#define LSAP        GPIO_NUM_4 // Limit switch de puerta abierta
#define LSCP        GPIO_NUM_5 // Limit switch de puerta cerrada
#define OPEN        GPIO_NUM_18 // Botón abrir
#define CLOSE       GPIO_NUM_19 // Botón cerrar
#define STOP        GPIO_NUM_21 // Botón Stop Emergency
#define MO_OPEN     GPIO_NUM_22 // Motor dirección abrir
#define MO_CLOSE    GPIO_NUM_23 // Motor dirección cerrar
#define LAMPARA     GPIO_NUM_2 // Lámpara
#define BUZZER      GPIO_NUM_15 // Buzzer
#define PPP         GPIO_NUM_16 // Botón Push-Push para abrir o cerrar dependiendo del estado




//Define de la maquina de estado

// Definición de constantes para los estados del sistema
#define ESTADO_INICIAL  0
#define ESTADO_CERRANDO 1
#define ESTADO_ABRIENDO 2
#define ESTADO_CERRADO  3
#define ESTADO_ABIERTO  4
#define ESTADO_ERR      5
#define ESTADO_STOP     6


// Definiciones de códigos de error
#define ERR_OK 0
#define ERR_OT 1
#define ERR_LSW 2


//Variables y Manejadores
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
esp_mqtt_client_handle_t client;
static const char *TAG = "State-Machine";

//variables globales de estado
int EstadoSiguiente = ESTADO_INICIAL;
int EstadoActual = ESTADO_INICIAL;
int EstadoAnterior = ESTADO_INICIAL;

// estado de lámpara intermitente
bool lampState = false;

// Estructura de entradas y salidas del sistema
struct IO
{
    unsigned int LSC:1; //entrada limitswitch de puerta cerrada
    unsigned int LSA:1; //entrada limitswitch de puerta abierta
    unsigned int BA:1;  //Boton abrir
    unsigned int BC:1;  //Boton cerrar
    unsigned int SE:1;  //Entrada de Stop Emergency
    unsigned int MA:1;  //Salida motor direccion abrir
    unsigned int MC:1;  //Salida motor direccion cerrar
    unsigned int lampara:1; //estado de la lámpara (1 = encendida, 0 = apagada)
    unsigned int buzzer:1; // estado del buzzer (1 = encendido, 0 = apagado)
    unsigned int PP:1;   // Botón Push-Push para abrir o cerrar dependiendo del estado
    unsigned int MQTT_CMD:2; // Entrada desde red WiFi/MQTT con 2 bits, lo que sería igual
} io;

// Estructura para variables de estado del sistema
struct STATUS
{
    unsigned int cntTimerCA;   // Contador para cierre automático (TCA)
    unsigned int cntRunTimer;  // Contador para tiempo máximo de movimiento
    int ERR_COD;               // Código de error actual
};

// Estructura de configuración del sistema
struct CONFIG
{
    unsigned int RunTimer;     // Tiempo máximo permitido para mover el portón
    unsigned int TimerCA;      // Tiempo para cierre automático
} config;

// Inicia status
struct STATUS status = {0, 0, ERR_OK};
// configura el tiempo del RunTimer (el primero) y el TimerCA (el segundo)
struct CONFIG config = {180, 100};


//Prototipos de funciones
void wifi_init_sta(void);
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void mqtt_app_start(void);
int mqtt_event_handler_cb(esp_mqtt_event_handle_t event);
int gpio_init();

//funciones de cada estado
int Func_ESTADO_INICIAL(void);
int Func_ESTADO_CERRANDO(void);
int Func_ESTADO_ABRIENDO(void);
int Func_ESTADO_CERRADO(void);
int Func_ESTADO_ABIERTO(void);
int Func_ESTADO_ERR(void);
int Func_ESTADO_STOP(void);

void TimerConfig(void);
void LAMPARA_ON_OFF(void * arg);
void TimerCallback (void * arg);


void app_main(void)
{
    wifi_init_sta();
    mqtt_app_start();
    gpio_init();
    TimerConfig();
    // Bucle infinito que ejecuta continuamente la máquina de estados
    for(;;){
        if(EstadoSiguiente == ESTADO_INICIAL)
        {
            EstadoSiguiente = Func_ESTADO_INICIAL();
        }

        if(EstadoSiguiente == ESTADO_ABIERTO)
        {
            EstadoSiguiente = Func_ESTADO_ABIERTO();
        }

        if(EstadoSiguiente == ESTADO_ABRIENDO)
        {
            EstadoSiguiente = Func_ESTADO_ABRIENDO();
        }

        if(EstadoSiguiente == ESTADO_CERRADO)
        {
            EstadoSiguiente = Func_ESTADO_CERRADO();
        }

        if(EstadoSiguiente == ESTADO_CERRANDO)
        {
            EstadoSiguiente = Func_ESTADO_CERRANDO();
        }

        if(EstadoSiguiente == ESTADO_ERR)
        {
            EstadoSiguiente = Func_ESTADO_ERR();
        }

        if(EstadoSiguiente == ESTADO_STOP)
        {
            EstadoSiguiente = Func_ESTADO_STOP();
        }
    }
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }

}

void wifi_init_sta(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

int mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        esp_mqtt_client_subscribe(client, "Adrian", 0);
        esp_mqtt_client_publish(client, TOPIC_1, "READY", 0, 1, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("\nTOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%*s\r\n",event->data_len,event->data);
        for (int i = 0; i <event->data_len; i++)  {event->data[i]='\0';}
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    mqtt_event_handler_cb(event_data);
}


void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
         .broker.address.uri = Broker_Uri,
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
    esp_mqtt_client_subscribe(client, TOPIC_1, 0);
}

int gpio_init(){
    // Configuración de los pines GPIO
    // Configuración de los pines GPIO
    gpio_set_direction(LSAP, GPIO_MODE_INPUT); // Limit switch de puerta abierta
    gpio_set_direction(LSCP, GPIO_MODE_INPUT); // Limit switch de puerta cerrada
    gpio_set_direction(OPEN, GPIO_MODE_INPUT); // Botón abrir
    gpio_set_direction(CLOSE, GPIO_MODE_INPUT); // Botón cerrar
    gpio_set_direction(STOP, GPIO_MODE_INPUT); // Botón Stop Emergency
    gpio_set_direction(PPP, GPIO_MODE_INPUT); // Botón Push-Push para abrir o cerrar dependiendo del estado

    // Configuración de los pines de salida
    gpio_set_direction(MO_OPEN, GPIO_MODE_OUTPUT); // Motor dirección abrir
    gpio_set_direction(MO_CLOSE, GPIO_MODE_OUTPUT); // Motor dirección cerrar
    gpio_set_direction(LAMPARA, GPIO_MODE_OUTPUT); // Lámpara
    gpio_set_direction(BUZZER, GPIO_MODE_OUTPUT); // Buzzer

    return ESP_OK;
}
int gpio_set_values(void)
{
    // Configuración de los niveles de los pines GPIO
    gpio_set_level(MO_OPEN, io.MA); // Motor dirección abrir
    gpio_set_level(MO_CLOSE, io.MC); // Motor dirección cerrar
    gpio_set_level(BUZZER, io.buzzer); // Buzzer
    return ESP_OK;
}   

int gpio_get_values(void)
{
    // Lectura de los valores de los pines GPIO
    io.LSA = gpio_get_level(LSAP); // Limit switch de puerta abierta
    io.LSC = gpio_get_level(LSCP); // Limit switch de puerta cerrada
    io.BA = gpio_get_level(OPEN); // Botón abrir
    io.BC = gpio_get_level(CLOSE); // Botón cerrar
    io.SE = gpio_get_level(STOP); // Botón Stop Emergency
    io.PP = gpio_get_level(PPP); // Botón Push-Push para abrir o cerrar dependiendo del estado

    return ESP_OK;
}


int Func_ESTADO_INICIAL(void)
{
    //funciones de estado estaticas (una sola vez)
    status.cntRunTimer = 0;//reinicio del timer

    io.LSA = true;
    io.LSC = true;

    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_INICIAL;
    
    printf("Estado INICIAL se dirige al ");

    //verifica si existe un error
    if(io.LSC == true && io.LSA == true)
    {
        status.ERR_COD = ERR_LSW;
        return ESTADO_ERR;
    }
    //puerta cerrada
    else if(io.LSC == true && io.LSA == false)
    {
        return ESTADO_CERRADO;
    }
    //puerta abierta
    else if(io.LSC == false && io.LSA == true)
    {
        return ESTADO_CERRANDO;
    }
    //puerta en estado desconocido
    return ESTADO_STOP;
}


int Func_ESTADO_CERRANDO(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_CERRANDO;

    //funciones de estado estaticas (una sola vez)
    status.cntRunTimer = 0;   //reinicio del timer
    io.MA = false;
    io.MC = true;
    io.BA = false;
    io.BC = false;
    io.buzzer = true;
    gpio_set_values(); // Actualiza los valores de las salidas
    //ciclo de estado
    for (;;)
    {

        if (io.LSC == true)
        {
            return ESTADO_CERRADO;
        }

        if (io.BA == true || io.BC == true)
        {
            return ESTADO_STOP;
        }
        //verifica error de run timer
        if (status.cntRunTimer > config.RunTimer)
        {
            status.ERR_COD = ERR_OT;
            return ESTADO_ERR;
        }
    }
}


int Func_ESTADO_ABRIENDO(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ABRIENDO;
    printf("==> Estado actual: ABRIENDO\n");

    //funciones de estado estaticas (una sola vez)
    status.cntRunTimer = 0;//reinicio del timer
    io.MA = true;
    io.MC = false;
    io.BA = false;
    io.BC = false;
    io.buzzer = true;
    //ciclo de estado
    gpio_set_values(); // Actualiza los valores de las salidas
    for(;;)
    {

        if(io.LSA == true)
        {
            return ESTADO_ABIERTO;
        }

        if(io.BA == true || io.BC == true)
        {
            return ESTADO_STOP;
        }

        //verifica error de run timer
        if(status.cntRunTimer > config.RunTimer)
        {
            status.ERR_COD = ERR_OT;
            return ESTADO_ERR;
        }
    }
}
int Func_ESTADO_CERRADO(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_CERRADO;
    printf("==> Estado actual: CERRADO\n");

    //funciones de estado estaticas (una sola vez)
    io.MA = false;
    io.MC = false;
    io.BA = false;
    io.buzzer = false;
    //ciclo de estado
    gpio_set_values(); // Actualiza los valores de las salidas
    for(;;)
    {
        if(io.BA == true || io.PP == true)
        {
            return ESTADO_ABRIENDO;
        }
        //boton PP abre
    }
}
int Func_ESTADO_ABIERTO(void)
{

    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ABIERTO;
    printf("==> Estado actual: ABIERTO\n");

    //funciones de estado estaticas (una sola vez)
    io.MA = false;
    io.MC = false;
    io.BC = false;
    io.lampara = true; // mantener encendida
    io.buzzer = false;
    status.cntTimerCA = 0; // reinicia el contador TCA al entrar
    gpio_set_values();
    //ciclo de estado
    for (;;)
    {
        printf("LÁMPARA: ENCENDIDO (puerta completamente abierta)\n");
        sleep(1); // espera 1 segundo
        status.cntTimerCA++; // incrementa el contador TCA

        if (status.cntTimerCA > 180 || io.BC == true ||io.PP == true) // si TCA > 3 min o si se presiona boton cerrar
        {
            return ESTADO_CERRANDO;
        }
        if (io.MA == false && io.MC == false && io.BC == false)
        {
            return ESTADO_ABIERTO;
        }
    }

}
int Func_ESTADO_ERR(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ERR;
    printf("==> Estado actual: ERROR\n\n");

    //funciones de estado estaticas (una sola vez)
    //Detener todo
    io.MA = false;
    io.MC = false;
    io.BA = false;
    io.BC = false;
    io.lampara = false;
    io.buzzer = false;

    // Variable para evitar imprimir múltiples veces
    bool mensajeMostrado = false;
    gpio_set_values();

    for (;;)
    {
        if (io.LSC == true && io.LSA == true)
        {
            status.ERR_COD = ERR_LSW;

            if (!mensajeMostrado)
            {
                printf("ERROR: Ambos limit switches activos (LSC y LSA).\n");
                mensajeMostrado = true;
            }
        }

        if (status.ERR_COD == ERR_LSW)
        {
            if ((io.LSC == false && io.LSA == true) || (io.LSC == true && io.LSA == false))
            {
                printf("Error corregido. Volviendo al estado inicial.\n");
                status.ERR_COD = ERR_OK;
                return ESTADO_INICIAL;
            }
        }

        if (status.ERR_COD == ERR_OT)
        {
            if (!mensajeMostrado)
            {
                printf("ERROR: han pasado más de 3 minutos con el portón abierto.\n");
                printf("Volviendo a cerrar el portón por seguridad.\n");
                mensajeMostrado = true;
            }
            return ESTADO_CERRANDO;
        }

    }
}

int Func_ESTADO_STOP(void)
{
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_STOP;
    printf("==> Estado: STOP / Emergencia\n");

    //funciones de estado estaticas (una sola vez)
    io.MA = false;
    io.MC = false;
    io.BA = false;
    io.BC = false;
    io.buzzer = false;
    gpio_set_values();
    //ciclo de estado
    for (;;)
    {
        // Si se presiona BA y la puerta no está completamente abierta
        if (io.BA == true && io.LSA == false)
        {
            return ESTADO_ABRIENDO;
        }

// Si se presiona BC y la puerta no está completamente cerrada
        if (io.BC == true && io.LSC == false)
        {
            return ESTADO_CERRANDO;
        }

// Si se presiona PP:
// - Si la puerta está abierta → cerrar
// - Si la puerta está cerrada → abrir
// - Si está en lugar desconocido → cerrar por seguridad
        if (io.PP == true)
        {
            if (io.LSA == true)
                return ESTADO_CERRANDO;
            else if (io.LSC == true)
                return ESTADO_ABRIENDO;
            else
                return ESTADO_CERRANDO;
        }

        if (io.LSC == true && io.LSA == true)
        {
            status.ERR_COD = ERR_OT;
            return ESTADO_ERR;
        }
        if (io.BA == true && io.BC == true)
        {
            return ESTADO_STOP;
        }


    }
}



//Se ejecuta cada 100 ms con el timer del micro. Su funcionamiento depende del micro
void TimerCallback (void * arg)
{
    status.cntRunTimer++;
    status.cntTimerCA++;
    gpio_get_values();
}

void LAMPARA_ON_OFF(void * arg)
{
    static int x = 0; // Variable estática para contar los ciclos de la lámpara
    if (EstadoActual == ESTADO_CERRADO)
    {
        gpio_set_level(LAMPARA, 0); // Apagar lámpara
        printf("Lámpara apagada\n");
    }
    else if (EstadoActual == ESTADO_ABIERTO)
    {
        gpio_set_level(LAMPARA, 1); // Encender lámpara
        printf("Lámpara encendida\n");
    }
    else if (EstadoActual == ESTADO_CERRANDO)
    {
            gpio_set_level(LAMPARA, !gpio_get_level(LAMPARA)); // Cambiar estado de la lámpara
            printf("Lámpara intermitente\n");
    }
    else if (EstadoActual == ESTADO_ABRIENDO)
    {
        x++;
        if(x % 2){
            gpio_set_level(LAMPARA, !gpio_get_level(LAMPARA)); // Cambiar estado de la lámpara
            printf("Lámpara intermitente\n");
            x = 0; 
        }
    }
    else if (EstadoActual == ESTADO_STOP)
    {
        gpio_set_level(LAMPARA, 0); // Apagar lámpara en estado de emergencia
        printf("Lámpara apagada\n");
    }
    else
    {
        gpio_set_level(LAMPARA, 0); // Apagar lámpara en otros estados
        printf("Lámpara apagada\n");
    }
}

// Configuración del timer para el microcontrolador
void TimerConfig()
{
    // Configuración del timer para el microcontrolador
    const esp_timer_create_args_t timer_args = {
        .callback = &TimerCallback,
        .name = "timer_callback"
    };
    esp_timer_handle_t periodic_timer_1;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &periodic_timer_1));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer_1, 100000)); // 100 ms

    const esp_timer_create_args_t lamp_timer_args = {
        .callback = &LAMPARA_ON_OFF,
        .name = "lamp_timer_callback"
    };
    esp_timer_handle_t periodic_timer_2;
    ESP_ERROR_CHECK(esp_timer_create(&lamp_timer_args, &periodic_timer_2));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer_2, 250000)); // 250 ms
}
