#include "Esp32MiniFridge.h"

#include "sdkconfig.h"
//#define _GLIBCXX_USE_C99

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "rom/rtc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdio.h>
#include <string>
#include "math.h"
#include "string.h"

#include "Config.h"
#include "I2SPlayer.h"
#include "Esp32MiniFridgeWebServer.h"
#include "FridgeController.h"
#include "Wifi.h"
#include "Storage.h"
#include "Url.h"
#include "WebClient.h"
#include "Ota.h"
#include "Monitoring.h"

#define ONBOARDLED_GPIO GPIO_NUM_13  // GPIO13 on Adafruit Huzzah, GPIO5 on Sparkfun ESP32 Thing, 

#define LOGTAG "main"

I2SPlayer musicPlayer;
Esp32MiniFridgeWebServer webServer;
Esp32MiniFridge esp32minifridge;
Storage storage;
Wifi wifi;
FridgeController fridgeController;
Monitoring monitoring;



Esp32MiniFridge::Esp32MiniFridge() {

	
}

Esp32MiniFridge::~Esp32MiniFridge() {

}

extern "C" {
void app_main();
}

void app_main() {
	nvs_flash_init();
	tcpip_adapter_init();
	esp32minifridge.Start();
}

//===========================================

void task_function_webserver(void *pvParameter) {
	((Esp32MiniFridge*) pvParameter)->TaskWebServer();
	vTaskDelete(NULL);
}

void task_function_fridgecontroller(void *pvParameter) {
	((Esp32MiniFridge*) pvParameter)->TaskFridgeController();
	vTaskDelete(NULL);
}

void task_function_resetbutton(void *pvParameter) {
	((Esp32MiniFridge*) pvParameter)->TaskResetButton();
	vTaskDelete(NULL);
}

void task_test_webclient(void *pvParameter) {
	((Esp32MiniFridge*) pvParameter)->TaskTestWebClient();
	vTaskDelete(NULL);
}

void task_function_restart(void* user_data) {
	ESP_LOGI(LOGTAG, "Restarting in 1secs....");
	vTaskDelay(*((int*)user_data)*1000 / portTICK_PERIOD_MS);
	esp_restart();
}

void Esp32MiniFridge::Restart(int seconds) {
	xTaskCreate(&task_function_restart, "restartTask", 2048, &seconds, 5, NULL);
}

//----------------------------------------------------------------------------------------

void Esp32MiniFridge::Start() {

	ESP_LOGI(LOGTAG, "Welcome to ESP32 Mini-Fridge");
	ESP_LOGI(LOGTAG, "ESP-IDF version %s", esp_get_idf_version());
	ESP_LOGI(LOGTAG, "Firmware version %s", FIRMWARE_VERSION);

	mConfig.Read();

	storage.Mount();

	//musicPlayer.init();

	fridgeController.init(mConfig.mbFridgePowerOn, mConfig.mfFridgeTargetTemperature);
	fridgeController.SetDeadBand(mConfig.mfFridgeDeadBand);

	mbButtonPressed = !gpio_get_level(GPIO_NUM_0);

	gpio_pad_select_gpio(10);
	gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
	gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);

	gpio_pad_select_gpio((gpio_num_t) ONBOARDLED_GPIO);
	gpio_set_direction((gpio_num_t) ONBOARDLED_GPIO, (gpio_mode_t) GPIO_MODE_OUTPUT);

	xTaskCreate(&task_function_fridgecontroller, "Task_FridgeController", 4096, this, 5, NULL);
	xTaskCreate(&task_function_webserver, "Task_WebServer", 8192, this, 5, NULL);
	xTaskCreate(&task_function_resetbutton, "Task_ResetButton", 2048, this, 5, NULL);

	ESP_LOGI(LOGTAG, "CONFIG HOSTNAME: %s", mConfig.msHostname.c_str() == NULL ? "NULL" : mConfig.msHostname.c_str());

	// Adafruit ESP32 Huzzah handling via Reset mode
	// long press reset button: rst::0x10 (RTCWDT_RTC_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
	// short press 

	//ESP_LOGD(LOGTAG, "RESET REASON CPU0=%i, CPU1=%i", rtc_get_reset_reason(0), rtc_get_reset_reason(1));
	//############## TODO --- does the huzzah need an external reset button!
	/*if (rtc_get_reset_reason(0) == RTCWDT_RTC_RESET) {
		ESP_LOGI(LOGTAG, "On-board reset button pressed... toggling Access Point mode and rebooting.");
		mConfig.ToggleAPMode();
		mConfig.Write();
		esp_restart();
	}*/


	if (mConfig.mbAPMode) {
		if (mConfig.muLastSTAIpAddress) {
			char sBuf[16];
			sprintf(sBuf, "%d.%d.%d.%d", IP2STR((ip4_addr* )&mConfig.muLastSTAIpAddress));
			ESP_LOGD(LOGTAG, "Last IP when connected to AP: %d : %s", mConfig.muLastSTAIpAddress, sBuf);
		}

		wifi.StartAPMode(mConfig.msAPSsid, mConfig.msAPPass, mConfig.msHostname);
		// start DNS server to always redirect any domain to 192.168.4.1
//		xTaskCreate(&task_function_dnsserver, "Task_DnsServer", 8192, this, 5, NULL);
	} else {
		wifi.SetNtpServer("pool.ntp.org");
		if (mConfig.msSTAENTUser.length())
			wifi.StartSTAModeEnterprise(mConfig.msSTASsid, mConfig.msSTAENTUser, mConfig.msSTAPass, mConfig.msSTAENTCA, mConfig.msHostname);
		else {
			wifi.StartSTAMode(mConfig.msSTASsid, mConfig.msSTAPass, mConfig.msHostname);
		}

		const char* hostname;
		tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_STA, &hostname);
		ESP_LOGI(LOGTAG, "Station hostname: %s", hostname);
		tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_AP, &hostname);
		ESP_LOGI(LOGTAG, "AP hostname: %s", hostname);
		ESP_LOGI(LOGTAG, "MDNS feature is disabled - no use found for it so far -- SSDP more interesting");
		//wifi.StartMDNS();
	}

	while (!wifi.IsConnected()){
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}

	monitoring.Start(mConfig.msDynatraceEnvironmentIdOrUrl, mConfig.msDynatraceApiToken, mConfig.msHostname);

	while (true) {
		TMeasurement measurement;
		measurement.timestamp = wifi.GetEpochMillisecondsUTC();
		if (measurement.timestamp) {
			measurement.temperature = fridgeController.GetActualTemperature();
			measurement.targettemp = fridgeController.GetTargetTemperature();
			measurement.cooling = fridgeController.IsCooling();
			measurement.power = fridgeController.IsPower();
			
			monitoring.Add(measurement);
		}
		vTaskDelay(10*1000 / portTICK_PERIOD_MS); // 10 second monitoring interval
	}
	
}

bool Esp32MiniFridge::StoreConfig() {
	mConfig.mbFridgePowerOn = fridgeController.IsPower();
	mConfig.mfFridgeDeadBand = fridgeController.GetDeadBand();
	mConfig.mfFridgeTargetTemperature = fridgeController.GetTargetTemperature();
	return mConfig.Write();
}

void Esp32MiniFridge::TaskTestWebClient() {
	Url url;
	WebClient webClient;
	//url.Selftest();

	/* url.Parse("http://www.msftconnecttest.com/connecttest.txt");
    webClient.Prepare(&url);
    webClient.AddHttpHeaderCStr("Connection: close");
    webClient.AddHttpHeaderCStr("Test1: testVal1");
    webClient.AddHttpHeaderCStr("Test2: testVal2; testVal3");
  	ESP_LOGW(LOGTAG, "Msftconnecttest Execute#1");
    if (!webClient.HttpGet()) {
      	ESP_LOGE(LOGTAG, "Error requesting: %s", url.GetUrl().c_str());
    }

  	ESP_LOGW(LOGTAG, "Msftconnecttest Execute#2");
    if (!webClient.HttpGet()) {
      	ESP_LOGE(LOGTAG, "Error requesting: %s", url.GetUrl().c_str());
    }
  	ESP_LOGW(LOGTAG, "Msftconnecttest Execute#3");
    if (!webClient.HttpGet()) {
      	ESP_LOGE(LOGTAG, "Error requesting: %s", url.GetUrl().c_str());
    }


  	ESP_LOGW(LOGTAG, "SSL CHECK: https://www.howsmyssl.com/a/check");
	url.Parse("https://www.howsmyssl.com/a/check");
    if (!webClient.Prepare(&url)) {
    	ESP_LOGE(LOGTAG, "Error in HttpPrepareGet()")
    }
  	ESP_LOGW(LOGTAG, "SSL CHECK BEFORE HTTPEXECUTE: https://www.howsmyssl.com/a/check");

    if (!webClient.HttpGet()) {
      	ESP_LOGE(LOGTAG, "Error in HttpExecute()")
    }

  	ESP_LOGW(LOGTAG, "SSL CHECK AFTER  HTTPEXECUTE: https://www.howsmyssl.com/a/check");

  	ESP_LOGW(LOGTAG, "BEGOIN https://httpbin.org/headers");
	url.Parse("http://httpbin.org/headers");
    webClient.Prepare(&url);
    webClient.HttpGet();
  	ESP_LOGW(LOGTAG, "END https://httpbin.org/headers");




  	ESP_LOGW(LOGTAG, "POST TEST: https://httpbin.org/post");
    url.Parse("http://httpbin.org/post");
    if (!webClient.Prepare(&url)) {
    	ESP_LOGE(LOGTAG, "Error in Preparing POST()")
    }

    std::string post = "{ example: \"data\" }";
  	ESP_LOGW(LOGTAG, "BEFORE POSTING");
    if (!webClient.HttpPost(post)) {
      	ESP_LOGE(LOGTAG, "Error in executing POST()")
    }
  	ESP_LOGW(LOGTAG, "AFTER POSTING"); */


/*	ESP_LOGW(LOGTAG, "#####Starting Firmware Update Task ....");

  	Ota ota;
    if(ota.UpdateFirmware("http://surpro4:9999/getfirmware")) {
	  	ESP_LOGI(LOGTAG, "#####AFTER OTA STUFF---- RESTARTING IN 2 SEC");
		vTaskDelay(2*1000 / portTICK_PERIOD_MS);
		esp_restart();
    } else {
    	//TODO add ota.GetErrorInfo() to inform end-user of problem
	  	ESP_LOGE(LOGTAG, "#####OTA update failed!");
    }*/

}

void Esp32MiniFridge::TaskWebServer() {
	while (true){
		if (wifi.IsConnected()){
			ESP_LOGI("Esp32MiniFridge", "starting Webserver");
			webServer.StartWebServer();
		}
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}

void Esp32MiniFridge::TaskFridgeController() {
	fridgeController.Run();
}


void Esp32MiniFridge::TaskDnsServer() {
//	dnsServer.start();
}

// blinkrate legend
// 1.8s on, 0.2s off   	wifi connected, cooling, all ok
// 0.2s on, 1.8s off	wifi connected, not cooling, all ok
// 0.2s on, 4.8s off	wifi connected, not cooling, all ok, but power off
// 0.5s on, 0.5s off,   wifi not connected, or in AP mode
// 0.1s on, 0,1s off,   error 
void Esp32MiniFridge::TaskResetButton() {
	int level = 0;
	int ticks = 0;

	while (true) {

		if (ticks <= 0) {
			if (wifi.IsConnected() && !mConfig.mbAPMode) {
				if (fridgeController.IsCooling() ? !level : level) {
					ticks = 18;
				} else {
					ticks = 2;
				}

				if (!fridgeController.IsPower() && level) {
				  ticks = 48;
				}

			} else {
				ticks = 5;
			}

			if (fridgeController.IsError()) {
				ticks = 1;
			}

			level = !level;
		}

		ticks--;

		gpio_set_level((gpio_num_t) ONBOARDLED_GPIO, (gpio_mode_t) level);

		vTaskDelay(100 / portTICK_PERIOD_MS);

		// this does not work for Adafruit Huzzah 
		/*
		if (!gpio_get_level(GPIO_NUM_0)) {
			if (!mbButtonPressed) {
				ESP_LOGI(LOGTAG, "Factory settings button pressed... rebooting into Access Point mode.");
				mConfig.ToggleAPMode();
				mConfig.Write();
				esp_restart();
			}
		} else {
			mbButtonPressed = false;
		}*/

	}
}

//-----------------------------------------------------------------------------------------

