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
        std::cout << "Neplatna odpoved.\n";
        return;
    }

    unsigned int mask = 0;
    mask |= ((unsigned int)data[0] << 24);
    mask |= ((unsigned int)data[1] << 16);
    mask |= ((unsigned int)data[2] << 8);
    mask |= (unsigned int)data[3];

    std::cout << "Podporovane PIDy v bloku 0x" << std::hex << std::uppercase << basePid
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
                std::cout << "Zadne ulozene DTC chyby.\n";
                return;
            }

            std::cout << "Ulozene DTC chyby:\n";
            for (size_t k = 0; k < codes.size(); ++k) {
                std::cout << "  " << codes[k] << "\n";
            }

            return;
        }
    }

    if (!found) {
        std::cout << "Nepodarilo se precist DTC nebo auto vratilo NO DATA.\n";
    }
}

void liveDashboard(SerialPort& port) {
    std::cout << "Live dashboard bezi. Ukoncis klavesou Q.\n\n";

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

        bool okRpm = readRPM(port, rpm);
        bool okSpeed = readSpeed(port, speed);
        bool okCoolant = readCoolantTemp(port, coolant);
        bool okIat = readIntakeAirTemp(port, iat);
        bool okThrottle = readThrottlePosition(port, throttle);
        bool okMaf = readMAF(port, maf);

        system("cls");
        std::cout << "===== LIVE DASHBOARD =====\n";
        std::cout << "Stiskni Q pro ukonceni\n\n";

        if (okRpm) {
            std::cout << "RPM:              " << rpm << "\n";
        }
        else {
            std::cout << "RPM:              N/A\n";
        }

        if (okSpeed) {
            std::cout << "Rychlost:         " << speed << " km/h\n";
        }
        else {
            std::cout << "Rychlost:         N/A\n";
        }

        if (okCoolant) {
            std::cout << "Teplota motoru:   " << coolant << " C\n";
        }
        else {
            std::cout << "Teplota motoru:   N/A\n";
        }

        if (okIat) {
            std::cout << "Teplota sani:     " << iat << " C\n";
        }
        else {
            std::cout << "Teplota sani:     N/A\n";
        }

        if (okThrottle) {
            std::cout << "Throttle:         " << throttle << " %\n";
        }
        else {
            std::cout << "Throttle:         N/A\n";
        }

        if (okMaf) {
            std::cout << "MAF:              " << maf << " g/s\n";
        }
        else {
            std::cout << "MAF:              N/A\n";
        }

        Sleep(700);
    }
}

void printMenu() {
    std::cout << "\n===== OBD MENU =====\n";
    std::cout << "1  - RPM\n";
    std::cout << "2  - Rychlost\n";
    std::cout << "3  - Teplota chladici kapaliny\n";
    std::cout << "4  - Teplota nasavaneho vzduchu\n";
    std::cout << "5  - Zatizeni motoru\n";
    std::cout << "6  - Poloha plynu\n";
    std::cout << "7  - MAF\n";
    std::cout << "8  - Napeti baterie / adapteru\n";
    std::cout << "9  - DTC chyby\n";
    std::cout << "10 - Podporovane PIDy\n";
    std::cout << "11 - Live dashboard\n";
    std::cout << "0  - Konec\n";
    std::cout << "Volba: ";
}

int main() {
    try {
        SerialPort port;
        std::string foundPortName;

        std::cout << "Hledam ELM327 na COM portech...\n";

        if (!findElm327Port(port, foundPortName)) {
            std::cerr << "ELM327 se nepodarilo najit.\n";
            std::cerr << "Zkontroluj:\n";
            std::cerr << "1. ze je adapter pripojeny\n";
            std::cerr << "2. ze je videt ve Spravci zarizeni\n";
            std::cerr << "3. ze je zapnute zapalovani\n";
            return 1;
        }

        std::cout << "Nalezen ELM327 na portu: " << foundPortName << "\n";
        std::cout << "Inicializace hotova.\n";

        while (true) {
            printMenu();

            int volba = -1;
            std::cin >> volba;

            if (!std::cin) {
                std::cin.clear();
                std::cin.ignore(10000, '\n');
                std::cout << "Neplatny vstup.\n";
                continue;
            }

            if (volba == 0) {
                break;
            }
            else if (volba == 1) {
                double rpm = 0.0;
                if (readRPM(port, rpm)) {
                    std::cout << "RPM: " << rpm << "\n";
                }
                else {
                    std::cout << "RPM se nepodarilo precist.\n";
                }
            }
            else if (volba == 2) {
                int speed = 0;
                if (readSpeed(port, speed)) {
                    std::cout << "Rychlost: " << speed << " km/h\n";
                }
                else {
                    std::cout << "Rychlost se nepodarilo precist.\n";
                }
            }
            else if (volba == 3) {
                int temp = 0;
                if (readCoolantTemp(port, temp)) {
                    std::cout << "Teplota chladici kapaliny: " << temp << " C\n";
                }
                else {
                    std::cout << "Teplotu se nepodarilo precist.\n";
                }
            }
            else if (volba == 4) {
                int temp = 0;
                if (readIntakeAirTemp(port, temp)) {
                    std::cout << "Teplota nasavaneho vzduchu: " << temp << " C\n";
                }
                else {
                    std::cout << "Teplotu nasavaneho vzduchu se nepodarilo precist.\n";
                }
            }
            else if (volba == 5) {
                double load = 0.0;
                if (readEngineLoad(port, load)) {
                    std::cout << "Zatizeni motoru: " << load << " %\n";
                }
                else {
                    std::cout << "Zatizeni motoru se nepodarilo precist.\n";
                }
            }
            else if (volba == 6) {
                double throttle = 0.0;
                if (readThrottlePosition(port, throttle)) {
                    std::cout << "Poloha plynu: " << throttle << " %\n";
                }
                else {
                    std::cout << "Polohu plynu se nepodarilo precist.\n";
                }
            }
            else if (volba == 7) {
                double maf = 0.0;
                if (readMAF(port, maf)) {
                    std::cout << "MAF: " << maf << " g/s\n";
                }
                else {
                    std::cout << "MAF se nepodarilo precist.\n";
                }
            }
            else if (volba == 8) {
                double voltage = 0.0;
                if (readVoltage(port, voltage)) {
                    std::cout << "Napeti: " << voltage << " V\n";
                }
                else {
                    std::cout << "Napeti se nepodarilo precist.\n";
                }
            }
            else if (volba == 9) {
                readDtcCodes(port);
            }
            else if (volba == 10) {
                showSupportedPids(port);
            }
            else if (volba == 11) {
                liveDashboard(port);
            }
            else {
                std::cout << "Neplatna volba.\n";
            }
        }

        std::cout << "Konec programu.\n";
    }
    catch (const std::exception& e) {
        std::cerr << "Chyba: " << e.what() << "\n";
        return 1;
    }

    return 0;
}