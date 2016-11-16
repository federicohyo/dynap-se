/*
 * Mac os X: g++ -std=c++11  -O2 -o dynapse_simple dynapse_simple.cpp -I/usr/local/include/ -L/usr/local/lib/ -lcaer
 * Linux: g++ -std=c++11 -pedantic -Wall -Wextra -O2 -o dynapse_simple dynapse_simple.cpp -D_POSIX_C_SOURCE=1 -D_BSD_SOURCE=1 -lcaer -I/usr/local/include/
 */

#include <libcaer/libcaer.h>
#include <libcaer/devices/dynapse.h>
#include <cstdio>
#include <csignal>
#include <atomic>
#include <fstream>
#include <iostream>

#define DEFAULTBIASES "data/defaultbiases_values.txt"
#define LOWPOWERBIASES "data/lowpowerbiases_values.txt"

using namespace std;

static atomic_bool globalShutdown(false);

static void globalShutdownSignalHandler(int signal) {
	// Simply set the running flag to false on SIGTERM and SIGINT (CTRL+C) for global shutdown.
	if (signal == SIGTERM || signal == SIGINT) {
		globalShutdown.store(true);
	}
}

int main(void) {
	// Install signal handler for global shutdown.
#if defined(_WIN32)
	if (signal(SIGTERM, &globalShutdownSignalHandler) == SIG_ERR) {
		caerLog(CAER_LOG_CRITICAL, "ShutdownAction", "Failed to set signal handler for SIGTERM. Error: %d.", errno);
		return (EXIT_FAILURE);
	}

	if (signal(SIGINT, &globalShutdownSignalHandler) == SIG_ERR) {
		caerLog(CAER_LOG_CRITICAL, "ShutdownAction", "Failed to set signal handler for SIGINT. Error: %d.", errno);
		return (EXIT_FAILURE);
	}
#else
	struct sigaction shutdownAction;

	shutdownAction.sa_handler = &globalShutdownSignalHandler;
	shutdownAction.sa_flags = 0;
	sigemptyset(&shutdownAction.sa_mask);
	sigaddset(&shutdownAction.sa_mask, SIGTERM);
	sigaddset(&shutdownAction.sa_mask, SIGINT);

	if (sigaction(SIGTERM, &shutdownAction, NULL) == -1) {
		caerLog(CAER_LOG_CRITICAL, "ShutdownAction",
				"Failed to set signal handler for SIGTERM. Error: %d.", errno);
		return (EXIT_FAILURE);
	}

	if (sigaction(SIGINT, &shutdownAction, NULL) == -1) {
		caerLog(CAER_LOG_CRITICAL, "ShutdownAction",
				"Failed to set signal handler for SIGINT. Error: %d.", errno);
		return (EXIT_FAILURE);
	}
#endif

	// Open the communication with Dynap-se, give it a device ID of 1, and don't care about USB bus or SN restrictions.
	caerDeviceHandle usb_handle = caerDeviceOpen(1, CAER_DEVICE_DYNAPSE, 0, 0,
			NULL);
	if (usb_handle == NULL) {
		return (EXIT_FAILURE);
	}

	// Let's take a look at the information we have on the device.
	struct caer_dynapse_info dynapse_info = caerDynapseInfoGet(usb_handle);

	printf("%s --- ID: %d, Master: %d,  Logic: %d.\n",
			dynapse_info.deviceString, dynapse_info.deviceID,
			dynapse_info.deviceIsMaster, dynapse_info.logicVersion);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	caerDeviceConfigSet(usb_handle, CAER_HOST_CONFIG_DATAEXCHANGE,
			CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
			DYNAPSE_CONFIG_CHIP_RUN, true);

	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN,
			true);
	// chip id is DYNAPSE_CONFIG_DYNAPSE_U2 
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID,
			DYNAPSE_CONFIG_DYNAPSE_U2);
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_MUX,
			DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE, true);

	ifstream input( DEFAULTBIASES);
	string::size_type sz;
    printf("Configuring silent biases...");
	for (std::string line; getline(input, line);) {
		int i_dec = atoi(line.c_str());
		caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_CONTENT, i_dec);
	}
	input.close();
    printf(" Done.\n");

    printf("Configuring sram content...");
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U2, 0);
    printf(" Done.\n");

    printf("Configuring cam content...");
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U2, 0);
    printf(" Done.\n");



    // close config
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, false);

    //close aer communication
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, false);
    //caerDeviceDataStop(usb_handle);

    caerDeviceClose(&usb_handle);


	// Open the communication with Dynap-se, give it a device ID of 1, and don't care about USB bus or SN restrictions.
	usb_handle = caerDeviceOpen(1, CAER_DEVICE_DYNAPSE, 0, 0,
			NULL);
	if (usb_handle == NULL) {
		return (EXIT_FAILURE);
	}

	// Let's take a look at the information we have on the device.
	dynapse_info = caerDynapseInfoGet(usb_handle);

	printf("%s --- ID: %d, Master: %d,  Logic: %d.\n",
			dynapse_info.deviceString, dynapse_info.deviceID,
			dynapse_info.deviceIsMaster, dynapse_info.logicVersion);


    caerDeviceConfigSet(usb_handle, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, true);

    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, true);

    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);

    // force chip to be enable even if aer is off
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE, true);

    printf("Configuring low power biases...");
	ifstream inputbiases( LOWPOWERBIASES);
	for (std::string line; getline(inputbiases, line);) {
		int i_dec = atoi(line.c_str());
		caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP,
				DYNAPSE_CONFIG_CHIP_CONTENT, i_dec);
	}
	inputbiases.close();
    printf(" Done.\n");

    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_MONITOR_NEU, 0, 0); // core 0 neu 0
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_MONITOR_NEU, 1, 0); // core 1 neu 0
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_MONITOR_NEU, 2, 0); // core 2 neu 0
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_MONITOR_NEU, 3, 0); // core 3 neu 0


	// Now let's get start getting some data from the device. We just loop, no notification needed.
	caerDeviceDataStart(usb_handle, NULL, NULL, NULL, NULL, NULL);
        printf("Start recorgind spikes...");
    
	while (!globalShutdown.load(memory_order_relaxed)) {
		caerEventPacketContainer packetContainer = caerDeviceDataGet(
				usb_handle);
		if (packetContainer == NULL) {
			continue; // Skip if nothing there.
		}

		int32_t packetNum = caerEventPacketContainerGetEventPacketsNumber(
				packetContainer);

		printf("\nGot event container with %d packets (allocated).\n",
				packetNum);

		for (int32_t i = 0; i < packetNum; i++) {
			caerEventPacketHeader packetHeader =
					caerEventPacketContainerGetEventPacket(packetContainer, i);
			if (packetHeader == NULL) {
				printf("Packet %d is empty (not present).\n", i);
				continue; // Skip if nothing there.
			}

			printf("Packet %d of type %d -> size is %d.\n", i,
					caerEventPacketHeaderGetEventType(packetHeader),
					caerEventPacketHeaderGetEventNumber(packetHeader));

			if (caerEventPacketHeaderGetEventType(packetHeader) == SPIKE_EVENT) {

				caerSpikeEventPacket evts = (caerSpikeEventPacket) packetHeader;

				// read all events
				CAER_SPIKE_ITERATOR_ALL_START(evts)

					uint64_t ts = caerSpikeEventGetTimestamp(
							caerSpikeIteratorElement);
					uint64_t neuronId = caerSpikeEventGetNeuronID(
							caerSpikeIteratorElement);
					uint64_t sourcecoreId = caerSpikeEventGetSourceCoreID(
							caerSpikeIteratorElement); // which core is from
					uint64_t coreId = caerSpikeEventGetChipID(
							caerSpikeIteratorElement);// destination core (used as chip id)

					printf("SPIKE: ts %d , neuronID: %d , sourcecoreID: %d, ascoreID: %d\n",ts, neuronId, sourcecoreId, coreId);

				CAER_SPIKE_ITERATOR_ALL_END
			}
		}

		caerEventPacketContainerFree(packetContainer);
	}

	caerDeviceDataStop(usb_handle);

	caerDeviceClose(&usb_handle);

	printf("Shutdown successful.\n");

	return (EXIT_SUCCESS);
}
