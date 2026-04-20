#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

class SerialPort {
public:
    SerialPort()
    {
        hSerial = INVALID_HANDLE_VALUE;
    }

    ~SerialPort() {
        close();
    }

    bool open(const std::string& portName, DWORD baudRate = CBR_38400) {
        close();

        hSerial = CreateFileA(
            portName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (hSerial == INVALID_HANDLE_VALUE) {
            return false;
        }

        DCB dcbSerialParams = { 0 };
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

        if (!GetCommState(hSerial, &dcbSerialParams)) {
            close();
            return false;
        }

        dcbSerialParams.BaudRate = baudRate;
        dcbSerialParams.ByteSize = 8;
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity = NOPARITY;

        if (!SetCommState(hSerial, &dcbSerialParams)) {
            close();
            return false;
        }

        COMMTIMEOUTS timeouts = { 0 };
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 300;
        timeouts.ReadTotalTimeoutMultiplier = 20;
        timeouts.WriteTotalTimeoutConstant = 300;
        timeouts.WriteTotalTimeoutMultiplier = 20;

        if (!SetCommTimeouts(hSerial, &timeouts)) {
            close();
            return false;
        }

        PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return true;
    }

    void close() {
        if (hSerial != INVALID_HANDLE_VALUE) {
            CloseHandle(hSerial);
            hSerial = INVALID_HANDLE_VALUE;
        }
    }

    bool isOpen() const {
        return hSerial != INVALID_HANDLE_VALUE;
    }

    bool writeLine(const std::string& cmd) {
        if (!isOpen()) {
            return false;
        }

        std::string fullCmd = cmd + "\r";
        DWORD bytesWritten = 0;

        if (!WriteFile(hSerial, fullCmd.c_str(), (DWORD)fullCmd.size(), &bytesWritten, nullptr)) {
            return false;
        }

        return true;
    }

    std::string readResponse() {
        if (!isOpen()) {
            return "";
        }

        std::string response;
        char buffer[256];
        DWORD bytesRead = 0;

        while (true) {
            if (!ReadFile(hSerial, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
                break;
            }

            if (bytesRead == 0) {
                break;
            }

            buffer[bytesRead] = '\0';
            response += buffer;

            if (response.find('>') != std::string::npos) {
                break;
            }
        }

        return response;
    }

    std::string query(const std::string& cmd, DWORD sleepMs = 150) {
        PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

        if (!writeLine(cmd)) {
            return "";
        }

        Sleep(sleepMs);
        return readResponse();
    }

private:
    HANDLE hSerial;
};

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start])) {
        start++;
    }

    size_t end = s.size();
    while (end > start && std::isspace((unsigned char)s[end - 1])) {
        end--;
    }

    return s.substr(start, end - start);
}

std::string toUpperCopy(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        s[i] = (char)std::toupper((unsigned char)s[i]);
    }
    return s;
}

std::string cleanElmResponse(const std::string& raw) {
    std::string result = raw;

    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    result.erase(std::remove(result.begin(), result.end(), '>'), result.end());

    return trim(result);
}

std::vector<int> hexBytesFromResponse(const std::string& resp) {
    std::vector<int> bytes;

    std::string cleaned = resp;
    std::replace(cleaned.begin(), cleaned.end(), '\r', ' ');
    std::replace(cleaned.begin(), cleaned.end(), '\n', ' ');
    std::replace(cleaned.begin(), cleaned.end(), '>', ' ');

    std::stringstream ss(cleaned);
    std::string token;

    while (ss >> token) {
        token = toUpperCopy(token);

        if (token == "SEARCHING..." || token == "NO" || token == "DATA" || token == "STOPPED") {
            continue;
        }

        if (token.size() == 2) {
            bool isHex = true;

            for (size_t i = 0; i < token.size(); ++i) {
                if (!std::isxdigit((unsigned char)token[i])) {
                    isHex = false;
                    break;
                }
            }

            if (isHex) {
                try {
                    int value = std::stoi(token, nullptr, 16);
                    bytes.push_back(value);
                }
                catch (...) {
                }
            }
        }
    }

    return bytes;
}

bool containsElmSignature(const std::string& response) {
    std::string upper = toUpperCopy(response);

    if (upper.find("ELM327") != std::string::npos) {
        return true;
    }

    if (upper.find("OBD") != std::string::npos) {
        return true;
    }

    if (upper.find("OK") != std::string::npos) {
        return true;
    }

    return false;
}

bool initElm327(SerialPort& port) {
    std::string resp;

    resp = port.query("ATZ", 800);
    if (resp.empty()) {
        return false;
    }

    resp = port.query("ATE0");
    if (resp.empty()) {
        return false;
    }

    resp = port.query("ATL0");
    if (resp.empty()) {
        return false;
    }

    resp = port.query("ATS0");
    if (resp.empty()) {
        return false;
    }

    resp = port.query("ATH0");
    if (resp.empty()) {
        return false;
    }

    resp = port.query("ATSP0", 300);
    if (resp.empty()) {
        return false;
    }

    return true;
}

bool findElm327Port(SerialPort& port, std::string& foundPortName) {
    std::vector<DWORD> baudRates;
    baudRates.push_back(CBR_38400);
    baudRates.push_back(CBR_9600);
    baudRates.push_back(CBR_115200);
    baudRates.push_back(CBR_57600);

    for (int i = 1; i <= 20; ++i) {
        std::string portName = "\\\\.\\COM" + std::to_string(i);

        for (size_t b = 0; b < baudRates.size(); ++b) {
            if (!port.open(portName, baudRates[b])) {
                continue;
            }

            Sleep(200);

            std::string resp = port.query("ATZ", 800);
            if (!resp.empty() && containsElmSignature(resp)) {
                foundPortName = portName;

                if (initElm327(port)) {
                    return true;
                }
            }

            resp = port.query("ATI", 300);
            if (!resp.empty() && containsElmSignature(resp)) {
                foundPortName = portName;

                if (initElm327(port)) {
                    return true;
                }
            }

            port.close();
        }
    }

    return false;
}

bool extractPidBytes(const std::string& resp, int modeReply, int pid, std::vector<int>& outBytes) {
    std::vector<int> bytes = hexBytesFromResponse(resp);

    for (size_t i = 0; i + 2 < bytes.size(); ++i) {
        if (bytes[i] == modeReply && bytes[i + 1] == pid) {
            outBytes.clear();
            for (size_t j = i + 2; j < bytes.size(); ++j) {
                outBytes.push_back(bytes[j]);
            }
            return true;
        }
    }

    return false;
}

bool readPidRaw(SerialPort& port, const std::string& cmd, std::string& rawResp) {
    rawResp = port.query(cmd, 200);

    if (rawResp.empty()) {
        return false;
    }

    std::string upper = toUpperCopy(rawResp);
    if (upper.find("NO DATA") != std::string::npos) {
        return false;
    }

    return true;
}

bool readRPM(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "010C", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x0C, data)) {
        return false;
    }

    if (data.size() < 2) {
        return false;
    }

    value = ((data[0] * 256) + data[1]) / 4.0;
    return true;
}

bool readSpeed(SerialPort& port, int& value) {
    std::string resp;
    if (!readPidRaw(port, "010D", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x0D, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = data[0];
    return true;
}

bool readCoolantTemp(SerialPort& port, int& value) {
    std::string resp;
    if (!readPidRaw(port, "0105", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x05, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = data[0] - 40;
    return true;
}

bool readIntakeAirTemp(SerialPort& port, int& value) {
    std::string resp;
    if (!readPidRaw(port, "010F", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x0F, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = data[0] - 40;
    return true;
}

bool readEngineLoad(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "0104", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x04, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = (data[0] * 100.0) / 255.0;
    return true;
}

bool readThrottlePosition(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "0111", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x11, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = (data[0] * 100.0) / 255.0;
    return true;
}

bool readMAF(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "0110", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x10, data)) {
        return false;
    }

    if (data.size() < 2) {
        return false;
    }

    value = ((data[0] * 256) + data[1]) / 100.0;
    return true;
}

bool readVoltage(SerialPort& port, double& value) {
    std::string resp = port.query("ATRV", 200);
    std::string cleaned = cleanElmResponse(resp);

    if (cleaned.empty()) {
        return false;
    }

    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), 'V'), cleaned.end());
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), 'v'), cleaned.end());

    try {
        value = std::stod(cleaned);
        return true;
    }
    catch (...) {
        return false;
    }
}

void printSupportedPidsBlock(const std::vector<int>& data, int basePid) {
    if (data.size() < 4) {
        std::cout << "Invalid response.\n";
        return;
    }

    unsigned int mask = 0;
    mask |= ((unsigned int)data[0] << 24);
    mask |= ((unsigned int)data[1] << 16);
    mask |= ((unsigned int)data[2] << 8);
    mask |= (unsigned int)data[3];

    std::cout << "Supported PIDs in block 0x" << std::hex << std::uppercase << basePid
              << "-0x" << (basePid + 0x1F) << std::dec << ":\n";

    for (int bit = 0; bit < 32; ++bit) {
        unsigned int test = 1u << (31 - bit);
        if ((mask & test) != 0) {
            int pid = basePid + bit + 1;
            std::cout << "  0x" << std::hex << std::uppercase << pid << std::dec << "\n";
        }
    }
}

void showSupportedPids(SerialPort& port) {
    std::vector<std::string> cmds;
    cmds.push_back("0100");
    cmds.push_back("0120");
    cmds.push_back("0140");
    cmds.push_back("0160");

    std::vector<int> bases;
    bases.push_back(0x00);
    bases.push_back(0x20);
    bases.push_back(0x40);
    bases.push_back(0x60);

    for (size_t i = 0; i < cmds.size(); ++i) {
        std::string resp;
        if (!readPidRaw(port, cmds[i], resp)) {
            continue;
        }

        std::vector<int> data;
        int replyPid = bases[i];
        if (!extractPidBytes(resp, 0x41, replyPid, data)) {
            continue;
        }

        printSupportedPidsBlock(data, bases[i]);
        std::cout << "\n";
    }
}

std::string decodeDtc(int b1, int b2) {
    char firstChar = 'P';
    int systemBits = (b1 & 0xC0) >> 6;

    if (systemBits == 0) {
        firstChar = 'P';
    }
    else if (systemBits == 1) {
        firstChar = 'C';
    }
    else if (systemBits == 2) {
        firstChar = 'B';
    }
    else if (systemBits == 3) {
        firstChar = 'U';
    }

    int digit2 = (b1 & 0x30) >> 4;
    int digit3 = (b1 & 0x0F);
    int digit4 = (b2 & 0xF0) >> 4;
    int digit5 = (b2 & 0x0F);

    std::string code;
    code += firstChar;
    code += char('0' + digit2);
    code += "0123456789ABCDEF"[digit3];
    code += "0123456789ABCDEF"[digit4];
    code += "0123456789ABCDEF"[digit5];

    return code;
}

void readDtcCodes(SerialPort& port) {
    std::string resp = port.query("03", 300);
    std::vector<int> bytes = hexBytesFromResponse(resp);

    bool found = false;

    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == 0x43) {
            found = true;

            std::vector<std::string> codes;

            for (size_t j = i + 1; j + 1 < bytes.size(); j += 2) {
                int b1 = bytes[j];
                int b2 = bytes[j + 1];

                if (b1 == 0x00 && b2 == 0x00) {
                    continue;
                }

                codes.push_back(decodeDtc(b1, b2));
            }

            if (codes.empty()) {
                std::cout << "No saved DTC errors.\n";
                return;
            }

            std::cout << "Saved DTC errors:\n";
            for (size_t k = 0; k < codes.size(); ++k) {
                std::cout << "  " << codes[k] << "\n";
            }

            return;
        }
    }

    if (!found) {
        std::cout << "Error reading or no data read.\n";
    }
}

bool readAbsoluteLoad(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "0143", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x43, data)) {
        return false;
    }

    if (data.size() < 2) {
        return false;
    }

    int raw = (data[0] * 256) + data[1];
    value = (raw * 100.0) / 255.0;
    return true;
}

bool readMAP(SerialPort& port, int& value) {
    std::string resp;
    if (!readPidRaw(port, "010B", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x0B, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = data[0];
    return true;
}

bool readBarometricPressure(SerialPort& port, int& value) {
    std::string resp;
    if (!readPidRaw(port, "0133", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x33, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = data[0];
    return true;
}

bool readCommandedEGR(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "012C", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x2C, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = (data[0] * 100.0) / 255.0;
    return true;
}

bool readEGRError(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "012D", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x2D, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = ((data[0] - 128) * 100.0) / 128.0;
    return true;
}

bool readFuelRailPressure(SerialPort& port, int& valueKPa) {
    std::string resp;
    if (!readPidRaw(port, "0123", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x23, data)) {
        return false;
    }

    if (data.size() < 2) {
        return false;
    }

    int raw = (data[0] * 256) + data[1];
    valueKPa = raw * 10;
    return true;
}

bool readCommandedThrottleActuator(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "014C", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x4C, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = (data[0] * 100.0) / 255.0;
    return true;
}

bool readRelativeThrottlePosition(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "0145", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x45, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = (data[0] * 100.0) / 255.0;
    return true;
}

bool readTimingAdvance(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "010E", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x0E, data)) {
        return false;
    }

    if (data.size() < 1) {
        return false;
    }

    value = (data[0] / 2.0) - 64.0;
    return true;
}

bool readRuntimeSinceStart(SerialPort& port, int& seconds) {
    std::string resp;
    if (!readPidRaw(port, "011F", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x1F, data)) {
        return false;
    }

    if (data.size() < 2) {
        return false;
    }

    seconds = (data[0] * 256) + data[1];
    return true;
}

bool readLambdaEqRatioBank1Sensor1(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "0124", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x24, data)) {
        return false;
    }

    if (data.size() < 2) {
        return false;
    }

    int raw = (data[0] * 256) + data[1];
    value = raw / 32768.0;
    return true;
}

bool readWidebandO2VoltageBank1Sensor1(SerialPort& port, double& value) {
    std::string resp;
    if (!readPidRaw(port, "0124", resp)) {
        return false;
    }

    std::vector<int> data;
    if (!extractPidBytes(resp, 0x41, 0x24, data)) {
        return false;
    }

    if (data.size() < 4) {
        return false;
    }

    int rawVoltage = (data[2] * 256) + data[3];
    value = rawVoltage / 8192.0;
    return true;
}

bool readBoostPressure(SerialPort& port, int& boostKPa) {
    int mapKPa = 0;
    int baroKPa = 0;

    if (!readMAP(port, mapKPa)) {
        return false;
    }

    if (!readBarometricPressure(port, baroKPa)) {
        return false;
    }

    boostKPa = mapKPa - baroKPa;
    return true;
}

double kPaToBar(int kPa) {
    return kPa / 100.0;
}


void liveDashboard(SerialPort& port) {
    std::cout << "Live dashboard is running. End with key Q.\n\n";

    while (true) {
        if (GetAsyncKeyState('Q') & 0x8000) {
            Sleep(300);
            break;
        }

        double rpm = 0.0;
        int speed = 0;
        int coolant = 0;
        int iat = 0;
        double throttle = 0.0;
        double maf = 0.0;
        double relLoad = 0.0;
        double absLoad = 0.0;
        int map = 0;
        int baro = 0;
        int boost = 0;
        double egr = 0.0;
        double egrErr = 0.0;
        int rail = 0;

        bool okRpm = readRPM(port, rpm);
        bool okSpeed = readSpeed(port, speed);
        bool okCoolant = readCoolantTemp(port, coolant);
        bool okIat = readIntakeAirTemp(port, iat);
        bool okThrottle = readThrottlePosition(port, throttle);
        bool okMaf = readMAF(port, maf);
        bool okRelLoad = readEngineLoad(port, relLoad);
        bool okAbsLoad = readAbsoluteLoad(port, absLoad);
        bool okMap = readMAP(port, map);
        bool okBaro = readBarometricPressure(port, baro);
        bool okBoost = readBoostPressure(port, boost);
        bool okEgr = readCommandedEGR(port, egr);
        bool okEgrErr = readEGRError(port, egrErr);
        bool okRail = readFuelRailPressure(port, rail);

        system("cls");
        std::cout << "===== LIVE TDi DASHBOARD =====\n";
        std::cout << "Press Q to end\n\n";

        std::cout << "RPM:                  " << (okRpm ? std::to_string((int)rpm) : "N/A") << "\n";
        std::cout << "Speed:                " << (okSpeed ? std::to_string(speed) + " km/h" : "N/A") << "\n";
        std::cout << "Coolant temp:         " << (okCoolant ? std::to_string(coolant) + " C" : "N/A") << "\n";
        std::cout << "Intake air temp:      " << (okIat ? std::to_string(iat) + " C" : "N/A") << "\n";
        std::cout << "Throttle pos:         " << (okThrottle ? std::to_string(throttle) + " %" : "N/A") << "\n";
        std::cout << "Engine load:          " << (okRelLoad ? std::to_string(relLoad) + " %" : "N/A") << "\n";
        std::cout << "Absolute load:        " << (okAbsLoad ? std::to_string(absLoad) + " %" : "N/A") << "\n";
        std::cout << "MAF:                  " << (okMaf ? std::to_string(maf) + " g/s" : "N/A") << "\n";
        std::cout << "MAP:                  " << (okMap ? std::to_string(map) + " kPa" : "N/A") << "\n";
        std::cout << "Barometric pressure:  " << (okBaro ? std::to_string(baro) + " kPa" : "N/A") << "\n";
        std::cout << "Boost pressure:       " << (okBoost ? std::to_string(boost) + " kPa" : "N/A") << "\n";
        std::cout << "Commanded EGR:        " << (okEgr ? std::to_string(egr) + " %" : "N/A") << "\n";
        std::cout << "EGR error:            " << (okEgrErr ? std::to_string(egrErr) + " %" : "N/A") << "\n";
        std::cout << "Fuel rail pressure:   " << (okRail ? std::to_string(rail) + " kPa" : "N/A") << "\n";

        Sleep(700);
    }
}

void printMenu() {
    std::cout << "\n===== OBD MENU =====\n";
    std::cout << "1  - RPM\n";
    std::cout << "2  - Speed\n";
    std::cout << "3  - Cooling liquid temp\n";
    std::cout << "4  - Intake air temp\n";
    std::cout << "5  - Engine load\n";
    std::cout << "6  - Throttle position\n";
    std::cout << "7  - MAF\n";
    std::cout << "8  - Battery voltage / adapter voltage\n";
    std::cout << "9  - DTC errors\n";
    std::cout << "10 - Supported PIDs\n";
    std::cout << "11 - Live dashboard\n";
    std::cout << "12 - MAP (intake manifold pressure)\n";
    std::cout << "13 - Barometric pressure\n";
    std::cout << "14 - Boost pressure\n";
    std::cout << "15 - Commanded EGR\n";
    std::cout << "16 - EGR error\n";
    std::cout << "17 - Absolute load\n";
    std::cout << "18 - Fuel rail pressure\n";
    std::cout << "19 - Timing advance\n";
    std::cout << "20 - Runtime since engine start\n";
    std::cout << "21 - Lambda eq ratio B1S1\n";
    std::cout << "22 - Wideband O2 voltage B1S1\n";
    std::cout << "23 - Commanded throttle actuator\n";
    std::cout << "24 - Relative throttle position\n";
    std::cout << "0  - End\n";
    std::cout << "choice: ";
}

int main() {
    try {
        SerialPort port;
        std::string foundPortName;

        std::cout << "Searching ELM327 on COM ports...\n";

        if (!findElm327Port(port, foundPortName)) {
            std::cerr << "ELM327 not found.\n";
            std::cerr << "Check:\n";
            std::cerr << "1. Adapter is connected\n";
            std::cerr << "2. is shown in device manager\n";
            std::cerr << "3. ignition is turned on\n";
            return 1;
        }

        std::cout << "Found ELM327 on port: " << foundPortName << "\n";
        std::cout << "Init complete.\n";

        while (true) {
            printMenu();

            int choice = -1;
            std::cin >> choice;

            if (!std::cin) {
                std::cin.clear();
                std::cin.ignore(10000, '\n');
                std::cout << "Invlalid value.\n";
                continue;
            }

            if (choice == 0) {
                break;
            }
            else if (choice == 1) {
                double rpm = 0.0;
                if (readRPM(port, rpm)) {
                    std::cout << "RPM: " << rpm << "\n";
                }
                else {
                    std::cout << "Error reading RPM.\n";
                }
            }
            else if (choice == 2) {
                int speed = 0;
                if (readSpeed(port, speed)) {
                    std::cout << "Speed: " << speed << " km/h\n";
                }
                else {
                    std::cout << "Error reading speed.\n";
                }
            }
            else if (choice == 3) {
                int temp = 0;
                if (readCoolantTemp(port, temp)) {
                    std::cout << "Cooling liquid temperature: " << temp << " C\n";
                }
                else {
                    std::cout << "Error reading cooling liquid temperature.\n";
                }
            }
            else if (choice == 4) {
                int temp = 0;
                if (readIntakeAirTemp(port, temp)) {
                    std::cout << "Intake air temp: " << temp << " C\n";
                }
                else {
                    std::cout << "Error reading intake air temperature.\n";
                }
            }
            else if (choice == 5) {
                double load = 0.0;
                if (readEngineLoad(port, load)) {
                    std::cout << "Engine load: " << load << " %\n";
                }
                else {
                    std::cout << "Error reading engine load.\n";
                }
            }
            else if (choice == 6) {
                double throttle = 0.0;
                if (readThrottlePosition(port, throttle)) {
                    std::cout << "Throttle position: " << throttle << " %\n";
                }
                else {
                    std::cout << "Error reading throttle position.\n";
                }
            }
            else if (choice == 7) {
                double maf = 0.0;
                if (readMAF(port, maf)) {
                    std::cout << "MAF: " << maf << " g/s\n";
                }
                else {
                    std::cout << "Error reading MAF.\n";
                }
            }
            else if (choice == 8) {
                double voltage = 0.0;
                if (readVoltage(port, voltage)) {
                    std::cout << "Voltage: " << voltage << " V\n";
                }
                else {
                    std::cout << "Error reading voltage.\n";
                }
            }
            else if (choice == 9) {
                readDtcCodes(port);
            }
            else if (choice == 10) {
                showSupportedPids(port);
            }
            else if (choice == 11) {
                liveDashboard(port);
            }
            else if (choice == 12) {
                int map = 0;
                if (readMAP(port, map)) {
                    std::cout << "MAP: " << map << " kPa\n";
                }
                else {
                    std::cout << "Error reading MAP.\n";
                }
            }
            else if (choice == 13) {
                int baro = 0;
                if (readBarometricPressure(port, baro)) {
                    std::cout << "Barometric pressure: " << baro << " kPa\n";
                }
                else {
                    std::cout << "Error reading barometric pressure.\n";
                }
            }
            else if (choice == 14) {
                int boost = 0;
                if (readBoostPressure(port, boost)) {
                    std::cout << "Boost pressure: " << boost << " kPa (" << kPaToBar(boost) << " bar)\n";
                }
                else {
                    std::cout << "Error reading boost pressure.\n";
                }
            }
            else if (choice == 15) {
                double egr = 0.0;
                if (readCommandedEGR(port, egr)) {
                    std::cout << "Commanded EGR: " << egr << " %\n";
                }
                else {
                    std::cout << "Error reading commanded EGR.\n";
                }
            }
            else if (choice == 16) {
                double egrErr = 0.0;
                if (readEGRError(port, egrErr)) {
                    std::cout << "EGR error: " << egrErr << " %\n";
                }
                else {
                    std::cout << "Error reading EGR error.\n";
                }
            }
            else if (choice == 17) {
                double load = 0.0;
                if (readAbsoluteLoad(port, load)) {
                    std::cout << "Absolute load: " << load << " %\n";
                }
                else {
                    std::cout << "Error reading absolute load.\n";
                }
            }
            else if (choice == 18) {
                int frp = 0;
                if (readFuelRailPressure(port, frp)) {
                    std::cout << "Fuel rail pressure: " << frp << " kPa\n";
                }
                else {
                    std::cout << "Error reading fuel rail pressure.\n";
                }
            }
            else if (choice == 19) {
                double adv = 0.0;
                if (readTimingAdvance(port, adv)) {
                    std::cout << "Timing advance: " << adv << " deg\n";
                }
                else {
                    std::cout << "Error reading timing advance.\n";
                }
            }
            else if (choice == 20) {
                int secs = 0;
                if (readRuntimeSinceStart(port, secs)) {
                    std::cout << "Runtime since start: " << secs << " s\n";
                }
                else {
                    std::cout << "Error reading runtime.\n";
                }
            }
            else if (choice == 21) {
                double lambda = 0.0;
                if (readLambdaEqRatioBank1Sensor1(port, lambda)) {
                    std::cout << "Lambda eq ratio B1S1: " << lambda << "\n";
                }
                else {
                    std::cout << "Error reading lambda eq ratio.\n";
                }
            }
            else if (choice == 22) {
                double o2v = 0.0;
                if (readWidebandO2VoltageBank1Sensor1(port, o2v)) {
                    std::cout << "Wideband O2 voltage B1S1: " << o2v << " V\n";
                }
                else {
                    std::cout << "Error reading wideband O2 voltage.\n";
                }
            }
            else if (choice == 23) {
                double throttleAct = 0.0;
                if (readCommandedThrottleActuator(port, throttleAct)) {
                    std::cout << "Commanded throttle actuator: " << throttleAct << " %\n";
                }
                else {
                    std::cout << "Error reading commanded throttle actuator.\n";
                }
            }
            else if (choice == 24) {
                double relThrottle = 0.0;
                if (readRelativeThrottlePosition(port, relThrottle)) {
                    std::cout << "Relative throttle position: " << relThrottle << " %\n";
                }
                else {
                    std::cout << "Error reading relative throttle position.\n";
                }
            }
        }

        std::cout << "Program finished.\n";
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}