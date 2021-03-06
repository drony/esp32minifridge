#ifndef MAIN_CONFIG_H_
#define MAIN_CONFIG_H_

#include "nvs.h"
#include "String.h"

class Config {
public:
	Config();
	virtual ~Config();

	bool Read();
	bool Write();

	void ToggleAPMode() { mbAPMode = !mbAPMode; };

private:
	bool ReadString(nvs_handle h, const char* sKey, String& rsValue);
	bool ReadBool(nvs_handle h, const char* sKey, bool& rbValue);
	bool ReadInt(nvs_handle h, const char* sKey, int& rbValue);
	bool WriteString(nvs_handle h, const char* sKey, String& rsValue);
	bool WriteBool(nvs_handle h, const char* sKey, bool bValue);
	bool WriteInt(nvs_handle h, const char* sKey, int bValue);


public:
	bool mbAPMode;

	String msAPSsid;
	String msAPPass;
	String msSTASsid;
	String msSTAPass;
	String msSTAENTUser;
	String msSTAENTCA;
	String msHostname;
	//String msUfoId;
	//String msUfoName;
	String msOrganization;
	String msDepartment;
	String msLocation;

    String msDynatraceEnvironmentIdOrUrl;
    String msDynatraceApiToken;
    int miDynatraceMonitoringInterval;
	bool mbDynatraceMonitoring;

	bool mbWebServerUseSsl;
	__uint16_t muWebServerPort;
	String msWebServerCert;

	__uint32_t muLastSTAIpAddress;

	// settings for fridge
	float mfFridgeTargetTemperature;
	bool  mbFridgePowerOn;
	float mfFridgeDeadBand;
};

#endif /* MAIN_CONFIG_H_ */
