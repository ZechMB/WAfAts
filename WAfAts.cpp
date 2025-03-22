/*
* Wooting analog keyboard inputs for ats and ets2
* based on 'input' example from scs sdk
*/

// Windows stuff.
#ifdef _WIN32
#  define WINVER 0x0500
#  define _WIN32_WINNT 0x0500
#  include <windows.h>
#endif

#include <string>
#include <fstream>

// SDK
#include "ScsSdk/include/scssdk_input.h"
#include "ScsSdk/include/eurotrucks2/scssdk_eut2.h"
#include "ScsSdk/include/eurotrucks2/scssdk_input_eut2.h"
#include "ScsSdk/include/amtrucks/scssdk_ats.h"
#include "ScsSdk/include/amtrucks/scssdk_input_ats.h"
#include "WootingSdkWrapper/includes/wooting-analog-wrapper.h"


#define UNUSED(x)

scs_log_t game_log = NULL;

//game only has 6 axes to set, 3 single and 3 dual
//gas, brake, clutch, steer, lookup, lookright
//should be safe to change if needed(just make sure your imported cfg is adapted as it will try to read more lines)
const int numOfAxes = 6;

// Prints message to game log.
// SCS_LOG_TYPE_message, SCS_LOG_TYPE_warning, SCS_LOG_TYPE_error
void log_line(const scs_log_type_t type, const char* const text, ...)
{
	if (!game_log) {
		return;
	}
	char formated[1000];

	va_list args;
	va_start(args, text);
	vsnprintf_s(formated, sizeof(formated), _TRUNCATE, text, args);
	formated[sizeof(formated) - 1] = 0;
	va_end(args);

	//prefix all of our messages
	char temp[1000] = "[plugin][WAfAts] ";
	strcat_s(temp, formated);
	game_log(type, temp);
}


struct device_data_t
{
	int nextReportedInput = 0;
	float lastReportedInputValues[numOfAxes] = { 0,0,0,0,0,0 };
};

device_data_t AnalogKeyboard;

//how many keys for each input axis
enum inputAxisType {
	disabled,
	single,
	dual,
};

struct inputData
{
	std::string displayName{ "unnamed axis" };
	unsigned short keyCode1{ 0 };
	unsigned short keyCode2{ 0 };
	inputAxisType type{ disabled };
};

inputData tableOfInputs[numOfAxes];

void sanitize(std::string& string, const char* whitelist)
{
	int badChar = string.find_first_not_of(whitelist);
	while (badChar >= 0) {
		string.erase(badChar, 1);
		badChar = string.find_first_not_of(whitelist);
	}
}

//fill tableOfInputs with user configurable inputs
void importInputs()
{
	const char whitelist[] = "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM1234567890 ._";
	const char whitelistNum[] = "1234567890";
	const char separators[] = ",";

	std::ifstream cfg("plugins/WAfAts.cfg");
	if (cfg.good()) {
		//do for each line of cfg
		for (int i{ 0 }; i < numOfAxes; ++i) {
			char lineString[50];
			std::string tempString;
			char* token = NULL;
			char* nextToken = NULL;

			cfg.getline(&lineString[0], _countof(lineString));
			token = strtok_s(lineString, separators, &nextToken);
			//assign name
			if (token != NULL)
			{
				tempString = token;
				sanitize(tempString, whitelist);
				if (!tempString.empty()) {
					tableOfInputs[i].displayName = tempString;
				}
				token = strtok_s(NULL, separators, &nextToken);
			}
			//assign key1
			if (token != NULL)
			{
				tempString = token;
				sanitize(tempString, whitelistNum);
				if (!tempString.empty()) {
					tableOfInputs[i].keyCode1 = stoi(tempString);
					tableOfInputs[i].type = single;
				}
				token = strtok_s(NULL, separators, &nextToken);
			}
			//assign key2
			if (token != NULL)
			{
				tempString = token;
				sanitize(tempString, whitelistNum);
				if (!tempString.empty()) {
					tableOfInputs[i].keyCode2 = stoi(tempString);
					tableOfInputs[i].type = dual;
				}
			}
		}
		log_line(SCS_LOG_TYPE_message, "got user values from cfg file");
		cfg.close();
		//printing tableOfInputs, could remove to unclutter log
		for (int i{ 0 }; i < numOfAxes; ++i) {
			log_line(SCS_LOG_TYPE_message, "imported name %i is %s", i, tableOfInputs[i].displayName.c_str());
			log_line(SCS_LOG_TYPE_message, "imported key1 %i is %u", i, tableOfInputs[i].keyCode1);
			log_line(SCS_LOG_TYPE_message, "imported key2 %i is %u", i, tableOfInputs[i].keyCode2);
			log_line(SCS_LOG_TYPE_message, "imported type %i is %i", i, tableOfInputs[i].type);
		}
	}
	else {
		log_line(SCS_LOG_TYPE_warning, "failure reading cfg file, using default keys (WASD)");
		tableOfInputs[1] =	{"Analog key W", 26, 0, single};
		tableOfInputs[2] = {"Analog key S", 22, 0, single};
		tableOfInputs[3] = {"Analog key AD", 4, 7, dual};
	}
};


//read an analog key or handle error
float readDevicePressed(unsigned short keyCode)
{
	float keyValue;
	keyValue = wooting_analog_read_analog(keyCode);
	if (keyValue >= 0) { return keyValue; }
	else {
		log_line(SCS_LOG_TYPE_error, "failure reading analog key value, error code = %f", keyValue);
		return 0.0;
	}
}


//2 inputs 0 to 1, 1 output -1 to 1
//A and D need to be the same axis with D being positive and A negative
//set to output the greater value if both are partially pressed or no value if equally pressed
float calculateSharedAxis(float left, float right)
{
	if (left == right) { return 0.0; }
	else if (left > right) { return -left; }
	else { return right; }
}


//get key value based on input type
int getNextKeyChanged(device_data_t& device)
{
	for (int i{0}; i < numOfAxes; ++i) {
		float currentValue = 0;

		if (tableOfInputs[i].type == single) {
			currentValue = readDevicePressed(tableOfInputs[i].keyCode1);
		}
		else if (tableOfInputs[i].type == dual) {
			currentValue = calculateSharedAxis(readDevicePressed(tableOfInputs[i].keyCode1), readDevicePressed(tableOfInputs[i].keyCode2));
		}

		if (currentValue != device.lastReportedInputValues[i]) {
			device.nextReportedInput = i;
			device.lastReportedInputValues[i] = currentValue;
			return i;
		}
	}
	return -1;
}


//called repeatedly until it returns SCS_RESULT_not_found
SCSAPI_RESULT input_event_callback(scs_input_event_t* const event_info, const scs_u32_t flags, const scs_context_t context)
{
	device_data_t& device = *static_cast<device_data_t*>(context);

	if (flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_after_activation) {
		log_line(SCS_LOG_TYPE_message, "First call after activation");
	}

	//also seems to be called if event_info.value is changed
	if (flags & SCS_INPUT_EVENT_CALLBACK_FLAG_first_in_frame) {
		//if no inputs changed
		if (getNextKeyChanged(device) < 0) {
			return SCS_RESULT_not_found;
		}
	}
	//update a changed value
	event_info->input_index = device.nextReportedInput;
	event_info->value_float.value = device.lastReportedInputValues[device.nextReportedInput];
	return SCS_RESULT_ok;
}


// Input API initialization function.
SCSAPI_RESULT scs_input_init(const scs_u32_t version, const scs_input_init_params_t* const params)
{
	// We currently support only one version.
	if (version != SCS_INPUT_VERSION_1_00) {
		return SCS_RESULT_unsupported;
	}

	const scs_input_init_params_v100_t* const version_params = static_cast<const scs_input_init_params_v100_t*>(params);
	game_log = version_params->common.log;

	// Check application version.
	log_line(SCS_LOG_TYPE_message, "Game '%s' %u.%u", version_params->common.game_id, SCS_GET_MAJOR_VERSION(version_params->common.game_version), SCS_GET_MINOR_VERSION(version_params->common.game_version));

	if (strcmp(version_params->common.game_id, SCS_GAME_ID_EUT2) == 0) {

		// Below the minimum version there might be some missing features (only minor change) or incompatible values (major change).
		const scs_u32_t MINIMAL_VERSION = SCS_INPUT_EUT2_GAME_VERSION_1_00;
		if (version_params->common.game_version < MINIMAL_VERSION) {
			log_line(SCS_LOG_TYPE_warning, "WARNING: Too old version of the game, some features might behave incorrectly");
		}

		// Future versions are fine as long the major version is not changed.
		const scs_u32_t IMPLEMENTED_VERSION = SCS_INPUT_EUT2_GAME_VERSION_CURRENT;
		if (SCS_GET_MAJOR_VERSION(version_params->common.game_version) > SCS_GET_MAJOR_VERSION(IMPLEMENTED_VERSION)) {
			log_line(SCS_LOG_TYPE_warning, "WARNING: Too new major version of the game, some features might behave incorrectly");
		}
	}
	else if (strcmp(version_params->common.game_id, SCS_GAME_ID_ATS) == 0) {

		// Below the minimum version there might be some missing features (only minor change) or incompatible values (major change).
		const scs_u32_t MINIMAL_VERSION = SCS_INPUT_ATS_GAME_VERSION_1_00;
		if (version_params->common.game_version < MINIMAL_VERSION) {
			log_line(SCS_LOG_TYPE_warning, "WARNING: Too old version of the game, some features might behave incorrectly");
		}

		// Future versions are fine as long the major version is not changed.
		const scs_u32_t IMPLEMENTED_VERSION = SCS_INPUT_ATS_GAME_VERSION_CURRENT;
		if (SCS_GET_MAJOR_VERSION(version_params->common.game_version) > SCS_GET_MAJOR_VERSION(IMPLEMENTED_VERSION)) {
			log_line(SCS_LOG_TYPE_warning, "WARNING: Too new major version of the game, some features might behave incorrectly");
		}
	}
	else {
		log_line(SCS_LOG_TYPE_warning, "WARNING: Unsupported game, some features or values might behave incorrectly");
	}


	//setup wooting sdk
	int WootingResult;
	WootingResult = wooting_analog_initialise();
	if (WootingResult >= 0) {
		log_line(SCS_LOG_TYPE_message, "init wooting analog sdk, devices found = %d", WootingResult);
	}
	else {
		log_line(SCS_LOG_TYPE_error, "wooting analog sdk init failure, error code = %d", WootingResult);
		return SCS_RESULT_generic_error;
	}


	//setup ingame input type and names
	scs_input_device_input_t inputs[numOfAxes];

	//get user configurable inputs from cfg
	importInputs();

	//populate inputs[]
	std::string tempString[numOfAxes];
	for (int i{ 0 }; i < numOfAxes; ++i) {
		tempString[i] = ("woot" + std::to_string(i));
		inputs[i].name = tempString[i].c_str();
		inputs[i].display_name = tableOfInputs[i].displayName.c_str();
		inputs[i].value_type = SCS_VALUE_TYPE_float;
	}

	scs_input_device_t device_info;
	device_info.name = "wootdevice";
	device_info.display_name = "Wooting Analog sdk Device";
	device_info.type = SCS_INPUT_DEVICE_TYPE_generic;
	device_info.input_count = numOfAxes;
	device_info.inputs = inputs;
	device_info.input_active_callback = NULL;
	device_info.input_event_callback = input_event_callback;
	device_info.callback_context = &AnalogKeyboard;

	if (version_params->register_device(&device_info) != SCS_RESULT_ok) {

		// Registrations created by unsuccessfull initialization are
		// cleared automatically so we can simply exit.
		log_line(SCS_LOG_TYPE_error, "Unable to register device");
		return SCS_RESULT_generic_error;
	}

	return SCS_RESULT_ok;
}


// Input API deinitialization function.
SCSAPI_VOID scs_input_shutdown(void)
{
	// Any cleanup needed. The registrations will be removed automatically.
	wooting_analog_uninitialise();
	game_log = NULL;
}

// Cleanup
#ifdef _WIN32
BOOL APIENTRY DllMain(
	HMODULE module,
	DWORD  reason_for_call,
	LPVOID reseved
)
{
	if (reason_for_call == DLL_PROCESS_DETACH) {
		wooting_analog_uninitialise();
	}
	return TRUE;
}
#endif

#ifdef __linux__
void __attribute__((destructor)) unload(void)
{
	wooting_analog_uninitialise();
}
#endif
