/*
 * Mac os X: g++ -std=c++11  -O2 -o dynapse_simple_v1 dynapse_simple_v1.cpp -I/usr/local/include/ -L/usr/local/lib/ -lcaer
 * Linux: g++ -std=c++11 -O2 -o dynapse_simple dynapse_simple.cpp -lcaer
 */
 
#include <libcaer/libcaer.h>
#include <libcaer/devices/dynapse.h>
#include <cstdio>
#include <csignal>
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>

#include <thread>

// Networking
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define DEFAULTBIASES "data/defaultbiases_values.txt"
#define LOWPOWERBIASES "data/lowpowerbiases_values.txt"

using namespace std;

static atomic_bool globalShutdown(false);
int serverSocket = -1, clientSocket = -1;
int configSocket = -1, configClient = -1;


static void globalShutdownSignalHandler(int signal) {
	if (signal == SIGTERM || signal == SIGINT) {
		globalShutdown.store(true);
	}
}

void setupSignalHandlers() {
#if defined(_WIN32)
	signal(SIGTERM, &globalShutdownSignalHandler);
	signal(SIGINT, &globalShutdownSignalHandler);
#else
	struct sigaction shutdownAction {};
	shutdownAction.sa_handler = &globalShutdownSignalHandler;
	sigemptyset(&shutdownAction.sa_mask);
	shutdownAction.sa_flags = 0;

	sigaction(SIGTERM, &shutdownAction, nullptr);
	sigaction(SIGINT, &shutdownAction, nullptr);
#endif
}

bool loadBiases(caerDeviceHandle handle, const std::string &biasFile) {
	ifstream input(biasFile);
	if (!input.is_open()) {
		cerr << "Error opening bias file: " << biasFile << endl;
		return false;
	}

	for (string line; getline(input, line);) {
		istringstream iss(line);
		int biasValue;
		if (!(iss >> biasValue)) {
			cerr << "Invalid bias value: " << line << endl;
			continue;
		}
		caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, biasValue);
	}
	input.close();
	return true;
}

bool configureDevice(caerDeviceHandle handle, const std::string &biasFile) {
	printf("Applying biases from %s...\n", biasFile.c_str());
	if (!loadBiases(handle, biasFile)) {
		return false;
	}

	caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
	caerDeviceConfigSet(handle, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U2, 0);

	// Enable neuron monitors (example: neuron 0 from all cores)
	for (int core = 0; core < 4; core++) {
		caerDeviceConfigSet(handle, DYNAPSE_CONFIG_MONITOR_NEU, core, 0);
	}

	return true;
}

void setupSocketServer(int port = 9001) {
	serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(port);

	bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
	listen(serverSocket, 1);
	printf("Waiting for GUI to connect on port %d...\n", port);
	clientSocket = accept(serverSocket, nullptr, nullptr);
	printf("GUI connected.\n");
}

void setupConfigSocketServer(int port = 9002) {
    configSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    bind(configSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(configSocket, 1);
    printf("Waiting for config client on port %d...\n", port);
    configClient = accept(configSocket, nullptr, nullptr);
    printf("Config client connected.\n");
}



void configHandler(caerDeviceHandle handle) {
    char buffer[256];
    while (!globalShutdown.load()) {
        ssize_t len = recv(configClient, buffer, sizeof(buffer) - 1, 0);
        if (len > 0) {
            buffer[len] = '\0';
            std::string command(buffer);
            std::istringstream iss(command);
            std::string token;
            iss >> token;
            if (token == "LOAD") {
                std::string filename;
                iss >> filename;
                std::string path = "data/" + filename;
                std::cout << "Reconfiguring with bias file: " << path << std::endl;
                configureDevice(handle, path);
            } else {
                std::cerr << "Unknown command: " << command << std::endl;
            }
        }
    }
}



void closeSockets() {
	if (clientSocket != -1) close(clientSocket);
	if (serverSocket != -1) close(serverSocket);
	if (configClient != -1) close(configClient);
	if (configSocket != -1) close(configSocket);
}

void readSpikes(caerDeviceHandle handle) {
	printf("Starting spike monitoring...\n");

	caerDeviceDataStart(handle, NULL, NULL, NULL, NULL, NULL);

	while (!globalShutdown.load(memory_order_relaxed)) {
		caerEventPacketContainer packetContainer = caerDeviceDataGet(handle);
		if (packetContainer == NULL) {
			continue;
		}

		int32_t packetNum = caerEventPacketContainerGetEventPacketsNumber(packetContainer);
		for (int32_t i = 0; i < packetNum; i++) {
			caerEventPacketHeader packetHeader = caerEventPacketContainerGetEventPacket(packetContainer, i);
			if (packetHeader == NULL) {
				continue;
			}

			if (caerEventPacketHeaderGetEventType(packetHeader) == SPIKE_EVENT) {
				caerSpikeEventPacket evts = (caerSpikeEventPacket) packetHeader;
				CAER_SPIKE_ITERATOR_ALL_START(evts)
					uint64_t ts = caerSpikeEventGetTimestamp(caerSpikeIteratorElement);
					uint64_t neuronId = caerSpikeEventGetNeuronID(caerSpikeIteratorElement);
					uint64_t sourceCoreId = caerSpikeEventGetSourceCoreID(caerSpikeIteratorElement);
					uint64_t chipId = caerSpikeEventGetChipID(caerSpikeIteratorElement);

					char buffer[128];
					snprintf(buffer, sizeof(buffer), "%llu %llu %llu %llu\n", ts, neuronId, sourceCoreId, chipId);
					send(clientSocket, buffer, strlen(buffer), 0);
					printf("Sent spike: %s", buffer);  
				CAER_SPIKE_ITERATOR_ALL_END
			}
		}

		caerEventPacketContainerFree(packetContainer);
	}

	caerDeviceDataStop(handle);
	printf("Stopped spike monitoring.\n");
}

int main() {
	setupSignalHandlers();

	caerDeviceHandle usb_handle = caerDeviceOpen(1, CAER_DEVICE_DYNAPSE, 0, 0, NULL);
	if (usb_handle == NULL) {
		cerr << "Failed to open Dynapse device." << endl;
		return EXIT_FAILURE;
	}

	auto dynapse_info = caerDynapseInfoGet(usb_handle);
	printf("%s --- ID: %d, Master: %d, Logic: %d.\n",
	       dynapse_info.deviceString, dynapse_info.deviceID,
	       dynapse_info.deviceIsMaster, dynapse_info.logicVersion);

	caerDeviceConfigSet(usb_handle, CAER_HOST_CONFIG_DATAEXCHANGE,
	                    CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_MUX,
	                    DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE, true);

	// Apply initial silent biases
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, true);
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, true);

	if (!configureDevice(usb_handle, DEFAULTBIASES)) {
		caerDeviceClose(&usb_handle);
		return EXIT_FAILURE;
	}

	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, false);
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, false);

	// Reconfigure with low power biases before monitoring
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, true);
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, true);

	if (!configureDevice(usb_handle, LOWPOWERBIASES)) {
		caerDeviceClose(&usb_handle);
		return EXIT_FAILURE;
	}

	setupConfigSocketServer();   // Accept config client FIRST
	setupSocketServer();         // Then accept GUI stream
	
	// Launch config handler thread
	std::thread configThread(configHandler, usb_handle);

	readSpikes(usb_handle);     // Blocking loop
	
	globalShutdown.store(true);
	configThread.join();

	closeSockets();           // ← Clean up sockets

	caerDeviceClose(&usb_handle);
	printf("Shutdown successful.\n");
	return EXIT_SUCCESS;
}

