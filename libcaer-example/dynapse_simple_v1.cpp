/*
 * Mac os X: g++ -std=c++11  -O2 -o dynapse_simple_v1 dynapse_simple_v1.cpp -I/usr/local/include/ -L/usr/local/lib/ -lcaer
 * Linux: g++ -std=c++11 -O2 -o dynapse_simple_v1 dynapse_simple_v1.cpp -lcaer
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
#include <map>

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

// Biases currently loaded/applied → to allow SAVE later
std::map<std::string, std::pair<int, int>> currentBiasValues;

struct BiasFlags {
    uint32_t param;
    bool sexN;
    bool typeNormal;
    bool biasHigh;
};

static inline uint8_t coarseValueForward(uint8_t coarseRev) {
    if (coarseRev == 0) return 0;
    else if (coarseRev == 4) return 1;
    else if (coarseRev == 2) return 2;
    else if (coarseRev == 6) return 3;
    else if (coarseRev == 1) return 4;
    else if (coarseRev == 5) return 5;
    else if (coarseRev == 3) return 6;
    else if (coarseRev == 7) return 7;
    else return 0;  // Fallback
}

std::map<std::string, uint32_t> parameterMap = {
    // CHIP
    {"CHIP_RUN", DYNAPSE_CONFIG_CHIP_RUN},
    {"CHIP_ID", DYNAPSE_CONFIG_CHIP_ID},   // <--- ADD THIS LINE!
    {"CHIP_REQ_DELAY", DYNAPSE_CONFIG_CHIP_REQ_DELAY},
    {"CHIP_REQ_EXTENSION", DYNAPSE_CONFIG_CHIP_REQ_EXTENSION},

    // MUX
    {"MUX_RUN", DYNAPSE_CONFIG_MUX_RUN},
    {"MUX_TIMESTAMP_RUN", DYNAPSE_CONFIG_MUX_TIMESTAMP_RUN},
    {"MUX_FORCE_CHIP_BIAS_ENABLE", DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE},

    // AER
    {"AER_RUN", DYNAPSE_CONFIG_AER_RUN},
    {"AER_ACK_DELAY", DYNAPSE_CONFIG_AER_ACK_DELAY},
    {"AER_ACK_EXTENSION", DYNAPSE_CONFIG_AER_ACK_EXTENSION},
    {"AER_WAIT_ON_TRANSFER_STALL", DYNAPSE_CONFIG_AER_WAIT_ON_TRANSFER_STALL},
};

std::map<std::string, BiasFlags> biasFlagMap = {

    // ------- C0 -------
    {"C0_PULSE_PWLK_P", { DYNAPSE_CONFIG_BIAS_C0_PULSE_PWLK_P, false, true, true }},
    {"C0_PS_WEIGHT_INH_S_N", { DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_INH_S_N, true, true, true }},
    {"C0_PS_WEIGHT_INH_F_N", { DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_INH_F_N, true, true, true }},
    {"C0_PS_WEIGHT_EXC_S_N", { DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_EXC_S_N, true, true, true }},
    {"C0_PS_WEIGHT_EXC_F_N", { DYNAPSE_CONFIG_BIAS_C0_PS_WEIGHT_EXC_F_N, true, true, true }},
    {"C0_IF_RFR_N", { DYNAPSE_CONFIG_BIAS_C0_IF_RFR_N, true, true, true }},
    {"C0_IF_TAU1_N", { DYNAPSE_CONFIG_BIAS_C0_IF_TAU1_N, false, true, true }},
    {"C0_IF_AHTAU_N", { DYNAPSE_CONFIG_BIAS_C0_IF_AHTAU_N, true, true, true }},
    {"C0_IF_CASC_N", { DYNAPSE_CONFIG_BIAS_C0_IF_CASC_N, true, true, true }},
    {"C0_IF_TAU2_N", { DYNAPSE_CONFIG_BIAS_C0_IF_TAU2_N, true, true, true }},
    {"C0_IF_BUF_P", { DYNAPSE_CONFIG_BIAS_C0_IF_BUF_P, false, true, true }},
    {"C0_IF_AHTHR_N", { DYNAPSE_CONFIG_BIAS_C0_IF_AHTHR_N, true, true, true }},
    {"C0_IF_THR_N", { DYNAPSE_CONFIG_BIAS_C0_IF_THR_N, true, true, true }},
    {"C0_NPDPIE_THR_S_P", { DYNAPSE_CONFIG_BIAS_C0_NPDPIE_THR_S_P, false, true, true }},
    {"C0_NPDPIE_THR_F_P", { DYNAPSE_CONFIG_BIAS_C0_NPDPIE_THR_F_P, false, true, true }},
    {"C0_NPDPII_THR_F_P", { DYNAPSE_CONFIG_BIAS_C0_NPDPII_THR_F_P, false, true, true }},
    {"C0_NPDPII_THR_S_P", { DYNAPSE_CONFIG_BIAS_C0_NPDPII_THR_S_P, false, true, true }},
    {"C0_IF_NMDA_N", { DYNAPSE_CONFIG_BIAS_C0_IF_NMDA_N, true, true, true }},
    {"C0_IF_DC_P", { DYNAPSE_CONFIG_BIAS_C0_IF_DC_P, false, true, true }},
    {"C0_IF_AHW_P", { DYNAPSE_CONFIG_BIAS_C0_IF_AHW_P, false, true, true }},
    {"C0_NPDPII_TAU_S_P", { DYNAPSE_CONFIG_BIAS_C0_NPDPII_TAU_S_P, false, true, true }},
    {"C0_NPDPII_TAU_F_P", { DYNAPSE_CONFIG_BIAS_C0_NPDPII_TAU_F_P, false, true, true }},
    {"C0_NPDPIE_TAU_F_P", { DYNAPSE_CONFIG_BIAS_C0_NPDPIE_TAU_F_P, false, true, true }},
    {"C0_NPDPIE_TAU_S_P", { DYNAPSE_CONFIG_BIAS_C0_NPDPIE_TAU_S_P, false, true, true }},
    {"C0_R2R_P", { DYNAPSE_CONFIG_BIAS_C0_R2R_P, false, true, true }},

    // ------- C1 -------
    {"C1_PULSE_PWLK_P", { DYNAPSE_CONFIG_BIAS_C1_PULSE_PWLK_P, false, true, true }},
    {"C1_PS_WEIGHT_INH_S_N", { DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_INH_S_N, true, true, true }},
    {"C1_PS_WEIGHT_INH_F_N", { DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_INH_F_N, true, true, true }},
    {"C1_PS_WEIGHT_EXC_S_N", { DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_EXC_S_N, true, true, true }},
    {"C1_PS_WEIGHT_EXC_F_N", { DYNAPSE_CONFIG_BIAS_C1_PS_WEIGHT_EXC_F_N, true, true, true }},
    {"C1_IF_RFR_N", { DYNAPSE_CONFIG_BIAS_C1_IF_RFR_N, true, true, true }},
    {"C1_IF_TAU1_N", { DYNAPSE_CONFIG_BIAS_C1_IF_TAU1_N, false, true, true }},
    {"C1_IF_AHTAU_N", { DYNAPSE_CONFIG_BIAS_C1_IF_AHTAU_N, true, true, true }},
    {"C1_IF_CASC_N", { DYNAPSE_CONFIG_BIAS_C1_IF_CASC_N, true, true, true }},
    {"C1_IF_TAU2_N", { DYNAPSE_CONFIG_BIAS_C1_IF_TAU2_N, true, true, true }},
    {"C1_IF_BUF_P", { DYNAPSE_CONFIG_BIAS_C1_IF_BUF_P, false, true, true }},
    {"C1_IF_AHTHR_N", { DYNAPSE_CONFIG_BIAS_C1_IF_AHTHR_N, true, true, true }},
    {"C1_IF_THR_N", { DYNAPSE_CONFIG_BIAS_C1_IF_THR_N, true, true, true }},
    {"C1_NPDPIE_THR_S_P", { DYNAPSE_CONFIG_BIAS_C1_NPDPIE_THR_S_P, false, true, true }},
    {"C1_NPDPIE_THR_F_P", { DYNAPSE_CONFIG_BIAS_C1_NPDPIE_THR_F_P, false, true, true }},
    {"C1_NPDPII_THR_F_P", { DYNAPSE_CONFIG_BIAS_C1_NPDPII_THR_F_P, false, true, true }},
    {"C1_NPDPII_THR_S_P", { DYNAPSE_CONFIG_BIAS_C1_NPDPII_THR_S_P, false, true, true }},
    {"C1_IF_NMDA_N", { DYNAPSE_CONFIG_BIAS_C1_IF_NMDA_N, true, true, true }},
    {"C1_IF_DC_P", { DYNAPSE_CONFIG_BIAS_C1_IF_DC_P, false, true, true }},
    {"C1_IF_AHW_P", { DYNAPSE_CONFIG_BIAS_C1_IF_AHW_P, false, true, true }},
    {"C1_NPDPII_TAU_S_P", { DYNAPSE_CONFIG_BIAS_C1_NPDPII_TAU_S_P, false, true, true }},
    {"C1_NPDPII_TAU_F_P", { DYNAPSE_CONFIG_BIAS_C1_NPDPII_TAU_F_P, false, true, true }},
    {"C1_NPDPIE_TAU_F_P", { DYNAPSE_CONFIG_BIAS_C1_NPDPIE_TAU_F_P, false, true, true }},
    {"C1_NPDPIE_TAU_S_P", { DYNAPSE_CONFIG_BIAS_C1_NPDPIE_TAU_S_P, false, true, true }},
    {"C1_R2R_P", { DYNAPSE_CONFIG_BIAS_C1_R2R_P, false, true, true }},

    // ------- C2 -------
    {"C2_PULSE_PWLK_P", { DYNAPSE_CONFIG_BIAS_C2_PULSE_PWLK_P, false, true, true }},
    {"C2_PS_WEIGHT_INH_S_N", { DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_INH_S_N, true, true, true }},
    {"C2_PS_WEIGHT_INH_F_N", { DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_INH_F_N, true, true, true }},
    {"C2_PS_WEIGHT_EXC_S_N", { DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_EXC_S_N, true, true, true }},
    {"C2_PS_WEIGHT_EXC_F_N", { DYNAPSE_CONFIG_BIAS_C2_PS_WEIGHT_EXC_F_N, true, true, true }},
    {"C2_IF_RFR_N", { DYNAPSE_CONFIG_BIAS_C2_IF_RFR_N, true, true, true }},
    {"C2_IF_TAU1_N", { DYNAPSE_CONFIG_BIAS_C2_IF_TAU1_N, false, true, true }},
    {"C2_IF_AHTAU_N", { DYNAPSE_CONFIG_BIAS_C2_IF_AHTAU_N, true, true, true }},
    {"C2_IF_CASC_N", { DYNAPSE_CONFIG_BIAS_C2_IF_CASC_N, true, true, true }},
    {"C2_IF_TAU2_N", { DYNAPSE_CONFIG_BIAS_C2_IF_TAU2_N, true, true, true }},
    {"C2_IF_BUF_P", { DYNAPSE_CONFIG_BIAS_C2_IF_BUF_P, false, true, true }},
    {"C2_IF_AHTHR_N", { DYNAPSE_CONFIG_BIAS_C2_IF_AHTHR_N, true, true, true }},
    {"C2_IF_THR_N", { DYNAPSE_CONFIG_BIAS_C2_IF_THR_N, true, true, true }},
    {"C2_NPDPIE_THR_S_P", { DYNAPSE_CONFIG_BIAS_C2_NPDPIE_THR_S_P, false, true, true }},
    {"C2_NPDPIE_THR_F_P", { DYNAPSE_CONFIG_BIAS_C2_NPDPIE_THR_F_P, false, true, true }},
    {"C2_NPDPII_THR_F_P", { DYNAPSE_CONFIG_BIAS_C2_NPDPII_THR_F_P, false, true, true }},
    {"C2_NPDPII_THR_S_P", { DYNAPSE_CONFIG_BIAS_C2_NPDPII_THR_S_P, false, true, true }},
    {"C2_IF_NMDA_N", { DYNAPSE_CONFIG_BIAS_C2_IF_NMDA_N, true, true, true }},
    {"C2_IF_DC_P", { DYNAPSE_CONFIG_BIAS_C2_IF_DC_P, false, true, true }},
    {"C2_IF_AHW_P", { DYNAPSE_CONFIG_BIAS_C2_IF_AHW_P, false, true, true }},
    {"C2_NPDPII_TAU_S_P", { DYNAPSE_CONFIG_BIAS_C2_NPDPII_TAU_S_P, false, true, true }},
    {"C2_NPDPII_TAU_F_P", { DYNAPSE_CONFIG_BIAS_C2_NPDPII_TAU_F_P, false, true, true }},
    {"C2_NPDPIE_TAU_F_P", { DYNAPSE_CONFIG_BIAS_C2_NPDPIE_TAU_F_P, false, true, true }},
    {"C2_NPDPIE_TAU_S_P", { DYNAPSE_CONFIG_BIAS_C2_NPDPIE_TAU_S_P, false, true, true }},
    {"C2_R2R_P", { DYNAPSE_CONFIG_BIAS_C2_R2R_P, false, true, true }},

    // ------- C3 -------
    {"C3_PULSE_PWLK_P", { DYNAPSE_CONFIG_BIAS_C3_PULSE_PWLK_P, false, true, true }},
    {"C3_PS_WEIGHT_INH_S_N", { DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_INH_S_N, true, true, true }},
    {"C3_PS_WEIGHT_INH_F_N", { DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_INH_F_N, true, true, true }},
    {"C3_PS_WEIGHT_EXC_S_N", { DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_EXC_S_N, true, true, true }},
    {"C3_PS_WEIGHT_EXC_F_N", { DYNAPSE_CONFIG_BIAS_C3_PS_WEIGHT_EXC_F_N, true, true, true }},
    {"C3_IF_RFR_N", { DYNAPSE_CONFIG_BIAS_C3_IF_RFR_N, true, true, true }},
    {"C3_IF_TAU1_N", { DYNAPSE_CONFIG_BIAS_C3_IF_TAU1_N, false, true, true }},
    {"C3_IF_AHTAU_N", { DYNAPSE_CONFIG_BIAS_C3_IF_AHTAU_N, true, true, true }},
    {"C3_IF_CASC_N", { DYNAPSE_CONFIG_BIAS_C3_IF_CASC_N, true, true, true }},
    {"C3_IF_TAU2_N", { DYNAPSE_CONFIG_BIAS_C3_IF_TAU2_N, true, true, true }},
    {"C3_IF_BUF_P", { DYNAPSE_CONFIG_BIAS_C3_IF_BUF_P, false, true, true }},
    {"C3_IF_AHTHR_N", { DYNAPSE_CONFIG_BIAS_C3_IF_AHTHR_N, true, true, true }},
    {"C3_IF_THR_N", { DYNAPSE_CONFIG_BIAS_C3_IF_THR_N, true, true, true }},
    {"C3_NPDPIE_THR_S_P", { DYNAPSE_CONFIG_BIAS_C3_NPDPIE_THR_S_P, false, true, true }},
    {"C3_NPDPIE_THR_F_P", { DYNAPSE_CONFIG_BIAS_C3_NPDPIE_THR_F_P, false, true, true }},
    {"C3_NPDPII_THR_F_P", { DYNAPSE_CONFIG_BIAS_C3_NPDPII_THR_F_P, false, true, true }},
    {"C3_NPDPII_THR_S_P", { DYNAPSE_CONFIG_BIAS_C3_NPDPII_THR_S_P, false, true, true }},
    {"C3_IF_NMDA_N", { DYNAPSE_CONFIG_BIAS_C3_IF_NMDA_N, true, true, true }},
    {"C3_IF_DC_P", { DYNAPSE_CONFIG_BIAS_C3_IF_DC_P, false, true, true }},
    {"C3_IF_AHW_P", { DYNAPSE_CONFIG_BIAS_C3_IF_AHW_P, false, true, true }},
    {"C3_NPDPII_TAU_S_P", { DYNAPSE_CONFIG_BIAS_C3_NPDPII_TAU_S_P, false, true, true }},
    {"C3_NPDPII_TAU_F_P", { DYNAPSE_CONFIG_BIAS_C3_NPDPII_TAU_F_P, false, true, true }},
    {"C3_NPDPIE_TAU_F_P", { DYNAPSE_CONFIG_BIAS_C3_NPDPIE_TAU_F_P, false, true, true }},
    {"C3_NPDPIE_TAU_S_P", { DYNAPSE_CONFIG_BIAS_C3_NPDPIE_TAU_S_P, false, true, true }},
    {"C3_R2R_P", { DYNAPSE_CONFIG_BIAS_C3_R2R_P, false, true, true }},

    // ------- U + D -------
    {"U_BUFFER", { DYNAPSE_CONFIG_BIAS_U_BUFFER, false, true, true }},
    {"U_SSP", { DYNAPSE_CONFIG_BIAS_U_SSP, false, true, true }},
    {"U_SSN", { DYNAPSE_CONFIG_BIAS_U_SSN, true, true, true }},
    {"D_BUFFER", { DYNAPSE_CONFIG_BIAS_D_BUFFER, false, true, true }},
    {"D_SSP", { DYNAPSE_CONFIG_BIAS_D_SSP, false, true, true }},
    {"D_SSN", { DYNAPSE_CONFIG_BIAS_D_SSN, true, true, true }}

};

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
    std::ifstream input(biasFile);
    if (!input.is_open()) {
        std::cerr << "Error opening bias file: " << biasFile << std::endl;
        return false;
    }

    // Ensure bias generator is enabled
    caerDeviceConfigSet(handle, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE, true);

    // Detect format
    std::string firstLine;
    if (!std::getline(input, firstLine)) {
        std::cerr << "Empty bias file: " << biasFile << std::endl;
        return false;
    }

    input.clear();
    input.seekg(0, std::ios::beg);

    std::istringstream issTest(firstLine);
    int testInt;
    if (issTest >> testInt) {
        // Detected RAW → LOAD ONLY, do not touch currentBiasValues
        std::cout << "Biases loaded in RAW format from " << biasFile << std::endl;

        for (std::string line; std::getline(input, line);) {
            std::istringstream issRaw(line);
            uint32_t rawValue;
            if (!(issRaw >> rawValue)) {
                std::cerr << "Invalid RAW bias line: " << line << std::endl;
                continue;
            }

            caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, rawValue);
        }
    }
    else {
        // Detected NAMED → LOAD AND FILL currentBiasValues
        std::cout << "Biases loaded in NAMED format from " << biasFile << std::endl;
        currentBiasValues.clear();

        for (std::string line; std::getline(input, line);) {
            std::istringstream issNamed(line);
            std::string biasName;
            int coarse, fine;

            if (!(issNamed >> biasName >> coarse >> fine)) {
                std::cerr << "Invalid NAMED bias line: " << line << std::endl;
                continue;
            }

            auto it = biasFlagMap.find(biasName);
            if (it == biasFlagMap.end()) {
                std::cerr << "Unknown bias name: " << biasName << " → skipping." << std::endl;
                continue;
            }

            uint32_t param = it->second.param;

            caer_bias_dynapse biasStruct;
            biasStruct.biasAddress = param;
            biasStruct.coarseValue = coarse;
            biasStruct.fineValue = fine;
            biasStruct.enabled = true;
            biasStruct.sexN = it->second.sexN;
            biasStruct.typeNormal = it->second.typeNormal;
            biasStruct.biasHigh = it->second.biasHigh;

            uint32_t biasValue = caerBiasDynapseGenerate(biasStruct);
            caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, biasValue);

            currentBiasValues[biasName] = { coarse, fine };
        }
    }

    input.close();
    return true;
}



void saveBiases(const std::string &filename) {
    std::ofstream output(filename);
    if (!output.is_open()) {
        std::cerr << "Error opening file for writing: " << filename << std::endl;
        return;
    }

    for (const auto& entry : currentBiasValues) {
        const std::string& biasName = entry.first;
        int coarseReversed = entry.second.first;
        int fine = entry.second.second;

        // Important: convert reversed coarse back to "user" coarse
        int coarse = coarseValueForward(coarseReversed);

        output << biasName << " " << coarse << " " << fine << std::endl;
    }

    output.close();
    std::cout << "Biases saved to " << filename << std::endl;
}


bool configureDevice(caerDeviceHandle handle, const std::string &biasFile) {
	printf("Applying biases from %s...\n", biasFile.c_str());
	

	if (!loadBiases(handle, biasFile)) {
		return false;
	}
	
	// Enable neuron monitors (example: neuron 0 from all cores)
	caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
	for (int core = 0; core < 4; core++) {
		caerDeviceConfigSet(handle, DYNAPSE_CONFIG_MONITOR_NEU, core, 0);
	}
	caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
	for (int core = 0; core < 4; core++) {
		caerDeviceConfigSet(handle, DYNAPSE_CONFIG_MONITOR_NEU, core, 0);
	}
	caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
	for (int core = 0; core < 4; core++) {
		caerDeviceConfigSet(handle, DYNAPSE_CONFIG_MONITOR_NEU, core, 0);
	}
	caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
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
            //std::cout << "[DEBUG] token '" << token << "'" << std::endl;
            if (token == "LOAD") {
                std::string filename;
                iss >> filename;
                std::string path = "data/" + filename;
                std::cout << "Reconfiguring with bias file: " << path << std::endl;
                configureDevice(handle, path);
            } else if (token == "SET") {
                int core_id;
                std::string bias_base_name;
                int coarse, fine;
                iss >> core_id >> bias_base_name >> coarse >> fine;

                std::ostringstream full_bias_name;
                if (bias_base_name.rfind("C", 0) != 0 && bias_base_name != "U_BUFFER" && bias_base_name != "D_BUFFER" && bias_base_name != "U_SSP" && bias_base_name != "U_SSN" && bias_base_name != "D_SSP" && bias_base_name != "D_SSN") {
                    // Core-specific: auto-prefix with "C{core_id}_"
                    full_bias_name << "C" << core_id << "_" << bias_base_name;
                } else {
                    // U or D biases → leave as is
                    full_bias_name << bias_base_name;
                }

                std::string bias_name = full_bias_name.str();

                std::cout << "Setting bias: core " << core_id << " bias " << bias_name
                          << " coarse " << coarse << " fine " << fine << std::endl;

                auto it = biasFlagMap.find(bias_name);
                if (it == biasFlagMap.end()) {
                    std::cerr << "Unknown bias name: " << bias_name << std::endl;
                    continue;
                }

                // Compute correct bias param based on core
                uint32_t param = it->second.param;
                
                struct caer_bias_dynapse biasStruct;
                biasStruct.biasAddress = param; // Already correct!
                biasStruct.coarseValue = coarse;
                biasStruct.fineValue = fine;
                biasStruct.enabled = true;
                biasStruct.sexN = it->second.sexN;
                biasStruct.typeNormal = it->second.typeNormal;
                biasStruct.biasHigh = it->second.biasHigh;

                uint32_t bias_value = caerBiasDynapseGenerate(biasStruct);
                caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_CONTENT, bias_value);

                currentBiasValues[bias_name] = { coarse, fine };
                std::cout << "Bias applied." << std::endl;
            } else if (token == "SAVE") {
                std::string filename;
                iss >> filename;
                std::string path = "data/" + filename;
                cout << "Saving biases to file: " << path << endl;
                saveBiases(path);
            }else if (token == "PARAM_SET") {
                std::string moduleStr, paramStr;
                int value;
                iss >> moduleStr >> paramStr >> value;

                uint8_t module = 0;
                if (moduleStr == "CHIP") module = DYNAPSE_CONFIG_CHIP;
                else if (moduleStr == "MUX") module = DYNAPSE_CONFIG_MUX;
                else if (moduleStr == "AER") module = DYNAPSE_CONFIG_AER;
                else {
                    std::cerr << "Unknown module: " << moduleStr << std::endl;
                    continue;
                }

                auto it = parameterMap.find(paramStr);
                if (it == parameterMap.end()) {
                    std::cerr << "Unknown param: " << paramStr << std::endl;
                    continue;
                }

                std::cout << "Setting PARAM: " << moduleStr << " " << paramStr
                          << " = " << value << std::endl;

                caerDeviceConfigSet(handle, module, it->second, value);
            } else if (token == "MONITOR_SET") {
                int monitorId, coreId, neuronId;
                iss >> monitorId >> coreId >> neuronId;

                std::cout << "Setting MONITOR_NEU monitor=" << monitorId
                          << " core=" << coreId
                          << " neuron=" << neuronId << std::endl;

                caerDeviceConfigSet(handle, DYNAPSE_CONFIG_MONITOR_NEU, coreId, neuronId);
            } else if (token == "CAM_SET") {
                int inputNeuron, targetNeuron, camId, synType;
                iss >> inputNeuron >> targetNeuron >> camId >> synType;

                std::cout << "Writing CAM: InputNeuron=" << inputNeuron
                          << " TargetNeuron=" << targetNeuron
                          << " CAM_ID=" << camId
                          << " SynapseType=" << synType << std::endl;

                caerDynapseWriteCam(handle,
                    static_cast<uint16_t>(inputNeuron),
                    static_cast<uint16_t>(targetNeuron),
                    static_cast<uint8_t>(camId),
                    static_cast<uint8_t>(synType));
            }else if (token == "ROUTE_SET") {
                int chip, core, neuronCore, sramId, virtCore, sx, dx, sy, dy, destCore;
                iss >> chip >> core >> neuronCore >> sramId >> virtCore >> sx >> dx >> sy >> dy >> destCore;

                std::cout << "ROUTE_SET: CHIP=" << chip
                          << " CORE=" << core
                          << " NEURON=" << neuronCore
                          << " SRAM=" << sramId
                          << " VirtCore=" << virtCore
                          << " SX=" << sx << " DX=" << dx
                          << " SY=" << sy << " DY=" << dy
                          << " DEST=" << destCore << std::endl;

                // Set CHIP_ID first:
                caerDeviceConfigSet(handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, chip);

                // Compute global neuronId:
                uint16_t neuronId = caerDynapseCoreAddrToNeuronId(core, neuronCore);

                // Write SRAM:
                uint32_t sramWord = caerDynapseGenerateSramBits(
                    neuronId,
                    static_cast<uint8_t>(sramId),
                    static_cast<uint8_t>(virtCore),
                    static_cast<bool>(sx),
                    static_cast<uint8_t>(dx),
                    static_cast<bool>(sy),
                    static_cast<uint8_t>(dy),
                    static_cast<uint8_t>(destCore)
                );

                // Now perform write:
                caerDeviceConfigSet(handle, DYNAPSE_CONFIG_SRAM, DYNAPSE_CONFIG_SRAM_WRITEDATA, sramWord);
                caerDeviceConfigSet(handle, DYNAPSE_CONFIG_SRAM, DYNAPSE_CONFIG_SRAM_RWCOMMAND, DYNAPSE_CONFIG_SRAM_WRITE);
                caerDeviceConfigSet(handle, DYNAPSE_CONFIG_SRAM, DYNAPSE_CONFIG_SRAM_ADDRESS,
                                    neuronId * 4 + sramId); // Each neuron has 4 SRAMs

                std::cout << "SRAM write completed." << std::endl;
            }else if (token == "HELP") {
                std::cout << "Available biases:" << std::endl;
                for (const auto& entry : biasFlagMap) {
                    std::cout << "  " << entry.first << std::endl;
                }
            } else {
                std::cerr << "Unknown command: " << token << std::endl;
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
					//printf("Sent spike: %s", buffer);  
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

     caerDeviceConfigSet(usb_handle, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);
        
	// Apply initial silent biases
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, true);
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, true);

	if (!configureDevice(usb_handle, DEFAULTBIASES)) {
		caerDeviceClose(&usb_handle);
		return EXIT_FAILURE;
    }
	
	//caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, false);
	//caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, false);

    //caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, true);
    //caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, true);

    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
    // force chip to be enable even if aer is off
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE, true);
    
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
    // force chip to be enable even if aer is off
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE, true);

    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
    // force chip to be enable even if aer is off
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE, true);

    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
    // force chip to be enable even if aer is off
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_MUX, DYNAPSE_CONFIG_MUX_FORCE_CHIP_BIAS_ENABLE, true);
  
  
  
	// Clear all SRAM.
	/*printf("Clearing SRAM SRAM U0 ...\n");
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, 0, 0);
	printf("Clearing SRAM SRAM U1 ...\n");
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, 0, 0);
	printf("Clearing SRAM SRAM U2 ...\n");
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, 0, 0);
	printf("Clearing SRAM SRAM U3 ...\n");
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM_EMPTY, 0, 0);*/


    // Setup SRAM for USB monitoring of spike events.
    printf("Configuring sram content...");
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U0, 0);
    printf(" Done.\n");
    printf("Configuring cam content...");
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U0, 0);
    printf(" Done.\n");
    printf("Configuring sram content...");
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U1, 0);
    printf(" Done.\n");
    printf("Configuring cam content...");
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U1, 0);
    printf(" Done.\n");
    printf("Configuring sram content...");
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U2, 0);
    printf(" Done.\n");
    printf("Configuring cam content...");
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U2, 0);
    printf(" Done.\n");
      printf("Configuring sram content...");
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U3, 0);
    printf(" Done.\n");
    printf("Configuring cam content...");
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_DEFAULT_SRAM, DYNAPSE_CONFIG_DYNAPSE_U3, 0);
    printf(" Done.\n");
            
	// Reconfigure with low power biases before monitoring
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_RUN, true);
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_AER, DYNAPSE_CONFIG_AER_RUN, true);


    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U0);
	if (!configureDevice(usb_handle, LOWPOWERBIASES)) {
		caerDeviceClose(&usb_handle);
		return EXIT_FAILURE;
	}

    caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U1);
	if (!configureDevice(usb_handle, LOWPOWERBIASES)) {
		caerDeviceClose(&usb_handle);
		return EXIT_FAILURE;
	}
	
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U2);
	if (!configureDevice(usb_handle, LOWPOWERBIASES)) {
		caerDeviceClose(&usb_handle);
		return EXIT_FAILURE;
	}
	
	caerDeviceConfigSet(usb_handle, DYNAPSE_CONFIG_CHIP, DYNAPSE_CONFIG_CHIP_ID, DYNAPSE_CONFIG_DYNAPSE_U3);
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

