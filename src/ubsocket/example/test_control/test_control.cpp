/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
*/
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

static constexpr int ARGS_2 = 2;
static constexpr int ARGS_3 = 3;
static constexpr int ARGS_4 = 4;
static constexpr int ARGS_5 = 5;
static constexpr int ARGS_6 = 6;

int g_fd = -1; // Global socket file descriptor

void PrintMainMenu()
{
    std::cout << "\n=== Socket I/O Control Test Main Menu ===" << std::endl;
    std::cout << "1. Socket Management" << std::endl;
    std::cout << "2. fcntl Test" << std::endl;
    std::cout << "3. fcntl64 Test" << std::endl;
    std::cout << "4. ioctl Test" << std::endl;
    std::cout << "5. setsockopt Test" << std::endl;
    std::cout << "6. Show Current Status" << std::endl;
    std::cout << "0. Exit" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Select option (0-6): ";
}

void PrintSocketMenu()
{
    std::cout << "\n=== Socket Management Menu ===" << std::endl;
    std::cout << "1. Create New Socket" << std::endl;
    std::cout << "2. Close Current Socket" << std::endl;
    std::cout << "3. Return to Main Menu" << std::endl;
    std::cout << "===============================" << std::endl;
    std::cout << "Select option (1-3): ";
}

void PrintFcntlMenu()
{
    std::cout << "\n=== fcntl Test Menu ===" << std::endl;
    std::cout << "1. Get Current Flags" << std::endl;
    std::cout << "2. Set Non-Blocking Mode" << std::endl;
    std::cout << "3. Set Blocking Mode" << std::endl;
    std::cout << "4. Test Unsupported cmd (F_SETFD)" << std::endl;
    std::cout << "5. Run Complete fcntl Test" << std::endl;
    std::cout << "6. Return to Main Menu" << std::endl;
    std::cout << "=========================" << std::endl;
    std::cout << "Select option (1-6): ";
}

void PrintFcntl64Menu()
{
    std::cout << "\n=== fcntl64 Test Menu ===" << std::endl;
    std::cout << "1. Get Current Flags" << std::endl;
    std::cout << "2. Set Non-Blocking Mode" << std::endl;
    std::cout << "3. Set Blocking Mode" << std::endl;
    std::cout << "4. Test Unsupported cmd (F_SETFD)" << std::endl;
    std::cout << "5. Run Complete fcntl64 Test" << std::endl;
    std::cout << "6. Return to Main Menu" << std::endl;
    std::cout << "==========================" << std::endl;
    std::cout << "Select option (1-6): ";
}

void PrintIoctlMenu()
{
    std::cout << "\n=== ioctl Test Menu ===" << std::endl;
    std::cout << "1. Show Current Status" << std::endl;
    std::cout << "2. Set Non-Blocking Mode" << std::endl;
    std::cout << "3. Set Blocking Mode" << std::endl;
    std::cout << "4. Test Unsupported Request (FIONREAD)" << std::endl;
    std::cout << "5. Run Complete ioctl Test" << std::endl;
    std::cout << "6. Return to Main Menu" << std::endl;
    std::cout << "=========================" << std::endl;
    std::cout << "Select option (1-6): ";
}

void PrintSetsockoptMenu()
{
    std::cout << "\n=== setsockopt Test Menu ===" << std::endl;
    std::cout << "1. Set (SO_KEEPALIVE)" << std::endl;
    std::cout << "2. Return to Main Menu" << std::endl;
    std::cout << "=============================" << std::endl;
    std::cout << "Select option (1-2): ";
}

// Socket Management
void CreateNewSocket()
{
    if (g_fd >= 0) {
        std::cout << "Existing socket fd: " << g_fd << ", close it first..." << std::endl;
        close(g_fd);
    }
    
    g_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_fd < 0) {
        std::cout << "[ERROR] Failed to create socket!" << std::endl;
        return;
    }
    std::cout << "[SUCCESS] Created socket fd: " << g_fd << std::endl;
}

void CloseCurrentSocket()
{
    if (g_fd >= 0) {
        close(g_fd);
        std::cout << "[INFO] Closed socket fd: " << g_fd << std::endl;
        g_fd = -1;
    } else {
        std::cout << "[INFO] No socket to close" << std::endl;
    }
}

// fcntl
void FcntlGetFlags()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    int flags = fcntl(g_fd, F_GETFL, 0);
    std::cout << "Current flags: " << flags << std::endl;
    std::cout << "O_NONBLOCK value: " << O_NONBLOCK << std::endl;
    std::cout << "Non-blocking mode: " << ((flags & O_NONBLOCK) ? "YES" : "NO") << std::endl;
}

void FcntlSetNonBlock()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    int flags = fcntl(g_fd, F_GETFL, 0);
    fcntl(g_fd, F_SETFL, flags | O_NONBLOCK);
    std::cout << "[SUCCESS] Set to non-blocking mode via fcntl" << std::endl;
    FcntlGetFlags();
}

void FcntlSetBlock()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    int flags = fcntl(g_fd, F_GETFL, 0);
    fcntl(g_fd, F_SETFL, flags & ~O_NONBLOCK);
    std::cout << "[SUCCESS] Set to blocking mode via fcntl" << std::endl;
    FcntlGetFlags();
}

void FcntlTestUnsupported()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    std::cout << "[TEST] Testing unsupported F_SETFD command..." << std::endl;
    fcntl(g_fd, F_SETFD, 0);
    std::cout << "[INFO] F_SETFD command executed" << std::endl;
}

void FcntlFullTest()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    std::cout << "\n[START] Running complete fcntl test" << std::endl;
    std::cout << "--------------------------------" << std::endl;
    FcntlGetFlags();
    FcntlSetNonBlock();
    FcntlSetBlock();
    FcntlTestUnsupported();
    std::cout << "--------------------------------" << std::endl;
    std::cout << "[END] Complete fcntl test finished" << std::endl;
}


// fcntl64
void Fcntl64GetFlags()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    int flags = fcntl64(g_fd, F_GETFL, 0);
    std::cout << "Current flags (via fcntl64): " << flags << std::endl;
    std::cout << "O_NONBLOCK value: " << O_NONBLOCK << std::endl;
    std::cout << "Non-blocking mode: " << ((flags & O_NONBLOCK) ? "YES" : "NO") << std::endl;
}

void Fcntl64SetNonBlock()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    int flags = fcntl64(g_fd, F_GETFL, 0);
    fcntl64(g_fd, F_SETFL, flags | O_NONBLOCK);
    std::cout << "[SUCCESS] Set to non-blocking mode via fcntl64" << std::endl;
    FcntlGetFlags();
}

void Fcntl64SetBlock()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    int flags = fcntl64(g_fd, F_GETFL, 0);
    fcntl64(g_fd, F_SETFL, flags & ~O_NONBLOCK);
    std::cout << "[SUCCESS] Set to blocking mode via fcntl64" << std::endl;
    FcntlGetFlags();
}

void Fcntl64TestUnsupported()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    std::cout << "[TEST] Testing unsupported F_SETFD command (fcntl64)..." << std::endl;
    fcntl64(g_fd, F_SETFD, 0);
    std::cout << "[INFO] F_SETFD command executed via fcntl64" << std::endl;
}

void Fcntl64FullTest()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    std::cout << "\n[START] Running complete fcntl64 test" << std::endl;
    std::cout << "---------------------------------" << std::endl;
    Fcntl64GetFlags();
    Fcntl64SetNonBlock();
    Fcntl64SetBlock();
    Fcntl64TestUnsupported();
    std::cout << "---------------------------------" << std::endl;
    std::cout << "[END] Complete fcntl64 test finished" << std::endl;
}

// ioctl
void IoctlShowStatus()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    int flags = fcntl(g_fd, F_GETFL, 0);
    std::cout << "Current flags: " << flags << std::endl;
    std::cout << "Non-blocking mode: " << ((flags & O_NONBLOCK) ? "YES" : "NO") << std::endl;
}

void IoctlSetNonBlock()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    int nonBlock = 1;
    ioctl(g_fd, FIONBIO, &nonBlock);
    std::cout << "[SUCCESS] Set to non-blocking mode via ioctl(FIONBIO=1)" << std::endl;
    IoctlShowStatus();
}

void IoctlSetBlock()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    int nonBlock = 0;
    ioctl(g_fd, FIONBIO, &nonBlock);
    std::cout << "[SUCCESS] Set to blocking mode via ioctl(FIONBIO=0)" << std::endl;
    IoctlShowStatus();
}

void IoctlTestUnsupported()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    std::cout << "[TEST] Testing unsupported FIONREAD command..." << std::endl;
    int nbytes = 0;
    ioctl(g_fd, FIONREAD, &nbytes);
    std::cout << "[INFO] FIONREAD executed" << std::endl;
}

void IoctlFullTest()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    std::cout << "\n[START] Running complete ioctl test" << std::endl;
    std::cout << "--------------------------------" << std::endl;
    IoctlShowStatus();
    IoctlSetNonBlock();
    IoctlSetBlock();
    IoctlTestUnsupported();
    std::cout << "--------------------------------" << std::endl;
    std::cout << "[END] Complete ioctl test finished" << std::endl;
}

// setsockopt
void SetSockoptKeepAlive()
{
    if (g_fd < 0) {
        std::cout << "[WARNING] Please create a socket first" << std::endl;
        return;
    }
    
    int flag = 1;
    int result = setsockopt(g_fd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
    if (result != 0) {
        std::cout << "[ERROR] Failed to set SO_KEEPALIVE option" << std::endl;
    }
}

void ShowCurrentStatus()
{
    if (g_fd < 0) {
        std::cout << "[INFO] No active socket" << std::endl;
        return;
    }
    
    std::cout << "\n=== Current Socket Status ===" << std::endl;
    std::cout << "File Descriptor: " << g_fd << std::endl;
    
    int flags = fcntl(g_fd, F_GETFL, 0);
    std::cout << "Current flags: " << flags << std::endl;
    
    if (flags & O_NONBLOCK) {
        std::cout << "Mode: NON-BLOCKING" << std::endl;
    } else {
        std::cout << "Mode: BLOCKING" << std::endl;
    }
    std::cout << "=============================" << std::endl;
}

// Menu handlers
void HandleSocketMenu()
{
    int choice;
    do {
        PrintSocketMenu();
        std::cin >> choice;
        switch (choice) {
            case 1:
                CreateNewSocket();
                break;
            case ARGS_2:
                CloseCurrentSocket();
                break;
            case ARGS_3:
                std::cout << "[INFO] Returning to main menu..." << std::endl;
                break;
            default:
                std::cout << "[ERROR] Invalid selection. Please enter 1-3" << std::endl;
                break;
        }
        
        if (choice != ARGS_3) {
            std::cout << "\nPress Enter to continue...";
            std::cin.ignore();
            std::cin.get();
        }
    } while (choice != ARGS_3);
}

void HandleFcntlMenu()
{
    int choice;
    do {
        PrintFcntlMenu();
        std::cin >> choice;
        switch (choice) {
            case 1:
                FcntlGetFlags();
                break;
            case ARGS_2:
                FcntlSetNonBlock();
                break;
            case ARGS_3:
                FcntlSetBlock();
                break;
            case ARGS_4:
                FcntlTestUnsupported();
                break;
            case ARGS_5:
                FcntlFullTest();
                break;
            case ARGS_6:
                std::cout << "[INFO] Returning to main menu..." << std::endl;
                break;
            default:
                std::cout << "[ERROR] Invalid selection. Please enter 1-6" << std::endl;
                break;
        }
        
        if (choice != ARGS_6) {
            std::cout << "\nPress Enter to continue...";
            std::cin.ignore();
            std::cin.get();
        }
    } while (choice != ARGS_6);
}

void HandleFcntl64Menu()
{
    int choice;
    do {
        PrintFcntl64Menu();
        std::cin >> choice;
        switch (choice) {
            case 1:
                Fcntl64GetFlags();
                break;
            case ARGS_2:
                Fcntl64SetNonBlock();
                break;
            case ARGS_3:
                Fcntl64SetBlock();
                break;
            case ARGS_4:
                Fcntl64TestUnsupported();
                break;
            case ARGS_5:
                Fcntl64FullTest();
                break;
            case ARGS_6:
                std::cout << "[INFO] Returning to main menu..." << std::endl;
                break;
            default:
                std::cout << "[ERROR] Invalid selection. Please enter 1-6" << std::endl;
                break;
        }
        
        if (choice != ARGS_6) {
            std::cout << "\nPress Enter to continue...";
            std::cin.ignore();
            std::cin.get();
        }
    } while (choice != ARGS_6);
}

void HandleIoctlMenu()
{
    int choice;
    do {
        PrintIoctlMenu();
        std::cin >> choice;
        switch (choice) {
            case 1:
                IoctlShowStatus();
                break;
            case ARGS_2:
                IoctlSetNonBlock();
                break;
            case ARGS_3:
                IoctlSetBlock();
                break;
            case ARGS_4:
                IoctlTestUnsupported();
                break;
            case ARGS_5:
                IoctlFullTest();
                break;
            case ARGS_6:
                std::cout << "[INFO] Returning to main menu..." << std::endl;
                break;
            default:
                std::cout << "[ERROR] Invalid selection. Please enter 1-6" << std::endl;
                break;
        }
        
        if (choice != ARGS_6) {
            std::cout << "\nPress Enter to continue...";
            std::cin.ignore();
            std::cin.get();
        }
    } while (choice != ARGS_6);
}

void HandleSetsockoptMenu()
{
    int choice;
    do {
        PrintSetsockoptMenu();
        std::cin >> choice;
        switch (choice) {
            case 1:
                SetSockoptKeepAlive();
                break;
            case ARGS_2:
                std::cout << "[INFO] Returning to main menu..." << std::endl;
                break;
            default:
                std::cout << "[ERROR] Invalid selection. Please enter 1-2" << std::endl;
                break;
        }
        
        if (choice != ARGS_2) {
            std::cout << "\nPress Enter to continue...";
            std::cin.ignore();
            std::cin.get();
        }
    } while (choice != ARGS_2);
}

int main()
{
    std::cout << "================================================" << std::endl;
    std::cout << "    Socket I/O Control Interface Test Program" << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "[INFO] Program started. Creating initial socket..." << std::endl;
    CreateNewSocket();
    
    int mainChoice;
    do {
        PrintMainMenu();
        std::cin >> mainChoice;
        
        switch (mainChoice) {
            case 1:
                HandleSocketMenu();
                break;
            case ARGS_2:
                HandleFcntlMenu();
                break;
            case ARGS_3:
                HandleFcntl64Menu();
                break;
            case ARGS_4:
                HandleIoctlMenu();
                break;
            case ARGS_5:
                HandleSetsockoptMenu();
                break;
            case ARGS_6:
                ShowCurrentStatus();
                break;
            case 0:
                std::cout << "[INFO] Exiting program..." << std::endl;
                break;
            default:
                std::cout << "[ERROR] Invalid selection. Please enter 0-6" << std::endl;
                break;
        }
        
        if (mainChoice != 0) {
            std::cout << "\nPress Enter to continue...";
            std::cin.ignore();
            std::cin.get();
        }
    } while (mainChoice != 0);
    
    // Cleanup resources
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
        std::cout << "[INFO] Cleanup: Socket closed" << std::endl;
    }
    
    std::cout << "[INFO] Program terminated successfully" << std::endl;
    return 0;
}