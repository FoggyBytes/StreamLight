#include "PlatformSockets.h"
#include "Limelight-internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(NXDK)
#include <windows.h>
static LARGE_INTEGER clockFrequency;
#else
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#endif

uint64_t PltGetMicroseconds(void) {
#if defined(_WIN32) && !defined(NXDK)
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000ULL) / clockFrequency.QuadPart);
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000ULL + (uint64_t)now.tv_nsec / 1000;
#endif
}

static SOCKET receiverSocket;
static struct sockaddr_in receiverAddress;
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks;
uint16_t RtspPortNumber;

static void fail(const char* message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void sendDatagram(const void* data, int length) {
    SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender == INVALID_SOCKET) fail("sender socket creation");
    if (sendto(sender, (const char*)data, length, 0,
               (struct sockaddr*)&receiverAddress, sizeof(receiverAddress)) != length) {
        fail("sendto");
    }
    closeSocket(sender);
}

static void setupSockets(void) {
    SOCKADDR_LEN addressLength = sizeof(receiverAddress);
    memset(&receiverAddress, 0, sizeof(receiverAddress));
    receiverAddress.sin_family = AF_INET;
    receiverAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    receiverAddress.sin_port = 0;
    receiverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (receiverSocket == INVALID_SOCKET) fail("receiver socket creation");
    if (bind(receiverSocket, (struct sockaddr*)&receiverAddress, sizeof(receiverAddress)) != 0) {
        fail("bind");
    }
    if (getsockname(receiverSocket, (struct sockaddr*)&receiverAddress, &addressLength) != 0) {
        fail("getsockname");
    }
    if (setSocketNonBlocking(receiverSocket, true) != 0) fail("set nonblocking");
}

static void testEmptyZeroTimeout(void) {
    char buffer[64];
    if (recvUdpSocketWithTimeout(receiverSocket, buffer, sizeof(buffer), 0) != 0) {
        fail("empty zero-timeout receive did not return 0");
    }
}

static void testDrainBurst(void) {
    char payload[8], buffer[64];
    int i;
    for (i = 0; i < 32; i++) {
        memcpy(payload, &i, sizeof(i));
        sendDatagram(payload, sizeof(payload));
    }
    for (i = 0; i < 32; i++) {
        int received = recvUdpSocketWithTimeout(receiverSocket, buffer, sizeof(buffer), 0);
        if (received != (int)sizeof(payload)) fail("queued burst packet was not drained");
    }
    if (recvUdpSocketWithTimeout(receiverSocket, buffer, sizeof(buffer), 0) != 0) {
        fail("burst drain did not end at empty socket");
    }
}

static void testZeroLengthThenPayload(void) {
    const char payload[] = "after-empty";
    char buffer[64];
    sendDatagram(NULL, 0);
    sendDatagram(payload, sizeof(payload));
    if (recvUdpSocketWithTimeout(receiverSocket, buffer, sizeof(buffer), 0) != sizeof(payload) ||
            memcmp(buffer, payload, sizeof(payload)) != 0) {
        fail("zero-length datagram caused premature timeout or payload mismatch");
    }
}

#if defined(_WIN32) && !defined(NXDK)
static DWORD WINAPI delayedSender(void* unused) {
    (void)unused;
    Sleep(10);
    sendDatagram("delayed", 8);
    return 0;
}
#else
static void* delayedSender(void* unused) {
    (void)unused;
    usleep(10000);
    sendDatagram("delayed", 8);
    return NULL;
}
#endif

static void testDelayedPacket(void) {
    char buffer[64];
    uint64_t started = PltGetMicroseconds();
    int received;
#if defined(_WIN32) && !defined(NXDK)
    HANDLE thread = CreateThread(NULL, 0, delayedSender, NULL, 0, NULL);
    if (thread == NULL) fail("CreateThread");
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, delayedSender, NULL) != 0) fail("pthread_create");
#endif
    received = recvUdpSocketWithTimeout(receiverSocket, buffer, sizeof(buffer), 100);
#if defined(_WIN32) && !defined(NXDK)
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    if (received != 8 || memcmp(buffer, "delayed", 8) != 0) fail("delayed packet was not received");
    if (PltGetMicroseconds() - started < 5000) fail("delayed receive returned before sender delay");
}

int main(void) {
#if defined(_WIN32) && !defined(NXDK)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) fail("WSAStartup");
    QueryPerformanceFrequency(&clockFrequency);
#endif
    setupSockets();
    testEmptyZeroTimeout();
    testDrainBurst();
    testZeroLengthThenPayload();
    testEmptyZeroTimeout();
    testDelayedPacket();
    testEmptyZeroTimeout();
    closeSocket(receiverSocket);
#if defined(_WIN32) && !defined(NXDK)
    WSACleanup();
#endif
    puts("UDP receive timeout tests passed");
    return 0;
}
