#include "tor_controller.h"
#include <fstream>
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

bool TorController::Connect() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    socketFd_ = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (socketFd_ < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config_.controlPort));
    inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr);

    if (connect(socketFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        return false;
    }

    if (!Authenticate()) {
        Disconnect();
        return false;
    }

    connected_ = true;
    return true;
}

void TorController::Disconnect() {
    if (socketFd_ >= 0) {
#ifdef _WIN32
        closesocket(socketFd_);
#else
        close(socketFd_);
#endif
        socketFd_ = -1;
    }
    connected_ = false;
}

std::string TorController::ReadCookieAuthHex() {
    std::ifstream file(config_.cookieAuthFilePath, std::ios::binary);
    if (!file) return "";
    std::ostringstream hex;
    char byte;
    static const char* hexChars = "0123456789ABCDEF";
    while (file.get(byte)) {
        hex << hexChars[(static_cast<unsigned char>(byte) >> 4) & 0xF];
        hex << hexChars[static_cast<unsigned char>(byte) & 0xF];
    }
    return hex.str();
}

bool TorController::Authenticate() {
    std::string authCmd;
    if (!config_.controlPassword.empty()) {
        authCmd = "AUTHENTICATE \"" + config_.controlPassword + "\"\r\n";
    } else {
        std::string cookieHex = ReadCookieAuthHex();
        if (cookieHex.empty()) return false;
        authCmd = "AUTHENTICATE " + cookieHex + "\r\n";
    }

    std::string response = SendCommand(authCmd);
    return response.find("250 OK") != std::string::npos;
}

std::string TorController::SendCommand(const std::string& command) {
    if (socketFd_ < 0) return "";

    send(socketFd_, command.c_str(), static_cast<int>(command.size()), 0);

    char buffer[4096];
    std::string response;
    int bytesRead = recv(socketFd_, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response = buffer;
    }
    return response;
}

bool TorController::RequestNewIdentity() {
    if (!connected_) return false;
    std::string response = SendCommand("SIGNAL NEWNYM\r\n");
    return response.find("250 OK") != std::string::npos;
}

std::vector<std::string> TorController::GetCircuitStatus() {
    std::vector<std::string> lines;
    if (!connected_) return lines;

    std::string response = SendCommand("GETINFO circuit-status\r\n");
    std::istringstream stream(response);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line != "250 OK" && line[0] != '.') {
            lines.push_back(line);
        }
    }
    return lines;
}

bool TorController::IsBootstrapped() {
    if (!connected_) return false;
    std::string response = SendCommand("GETINFO status/bootstrap-phase\r\n");
    return response.find("PROGRESS=100") != std::string::npos;
}