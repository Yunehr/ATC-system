## ✈️ Project Overview
This system facilitates secure, state-driven communication using a custom-defined network protocol. The Flight Tower manages airspace safety via a robust state machine, while the Pilot application reports telemetry and requests vital flight documentation.
## Key Features
- Custom Packet Protocol: Implements a strict Head | Body | Tail structure with bit-field headers.
- State-Driven Logic: Server functionality is restricted based on operational states (e.g., Idle, Active, Emergency).
- Large Data Transfer: Reliable TCP-based transfer of a 1MB+ Flight Manual.
- Black Box Logging: Automated persistent logging of all TX/RX network traffic for safety audits.
  
## 🛠️ Tech Stack
- Language: Python 3.x
- Networking: TCP/IP (Sockets)
- Documentation: Doxygen
- Project Management: MS Teams & GitHub 👥

## 📈 Development Methodology
We employ an Agile-SCRUM methodology with two-week sprints.
- Parallel Development: Simultaneous work on Client and Server components.
- Quality Gate: Strict GitHub Pull Request (PR) rules requiring peer review for all merges to ensure code coverage and data integrity.
- TDD: Targeting 75%-80% code coverage to ensure all functional requirements are verified.

## 🚦Server State Machine
The Flight Tower operates through the following transitions
- IDLE: Waiting for Pilot authentication.
- PREFLIGHT: Validated pilot; preparing for taxi/takeoff.
- ACTIVE: Monitoring live telemetry and traffic.
- DATA_TRANSFER: Dedicated state for 1MB manual transmission.
- EMERGENCY: High-priority state triggered by "Mayday" commands.

## 📦 Data Packet Definition
- All packets follow the mandatory structure
- Header: 4-bit Source/Destination addresses and Packet Type.
- Body: Dynamically allocated payload containing telemetry or file fragments.
- Tail: 16-bit CRC for error detection and data integrity.

## Running the solution
- Requires gcc to compile C++ Backend
- Requires PyMuPDF to display PDF images
    - If not installed: `pip install PyMuPDF`

### Compile Executables
```
g++ .\server\*.hpp .\server\*.cpp -o .\build\server.exe .\shared\Packet.h .\shared\PacketTransport.cpp .\shared\PacketTransport.hpp .\shared\Request.h -lws2_32 

g++ .\client\*.hpp .\client\ClientApp.cpp .\client\ClientEngine.cpp .\client\FileReceiver.cpp .\client\Logger.cpp .\client\main.cpp -o .\build\clientCMD.exe .\shared\Packet.h .\shared\PacketTransport.cpp .\shared\PacketTransport.hpp .\shared\Request.h -lws2_32

g++ .\client\*.hpp .\client\ClientApp.cpp .\client\ClientEngine.cpp .\client\FileReceiver.cpp .\client\Logger.cpp .\client\clientAPI.cpp -o .\build\clientAPI.exe .\shared\Packet.h .\shared\PacketTransport.cpp .\shared\PacketTransport.hpp .\shared\Request.h -lws2_32

```
### Running Test suite
## server
```
cd tests

g++ -std=c++17 -o ServerTests.exe servertests.cpp `
   ../server/StateMachine.cpp `
   ../server/ServerEngine.cpp `
   ../shared/PacketTransport.cpp `
   ../server/WeatherService.cpp `
   ../server/FileTransferManager.cpp `
   ../server/ClientSession.cpp `
   "../external/googletest-1.17.0/googletest/src/gtest-all.cc" `
   -I"../external/googletest-1.17.0/googletest/include" `
   -I"../external/googletest-1.17.0/googletest" `
   -I"../server" `
   -I"../shared" `
   -lws2_32 -lpthread

./ServerTests.exe
```

## Client
```

cd tests

g++ -std=c++17 -o ClientTests.exe clienttests.cpp `
  ../client/FileReceiver.cpp `
  ../shared/PacketTransport.cpp `
  "../external/googletest-1.17.0/googletest/src/gtest-all.cc" `
  -I"../external/googletest-1.17.0/googletest/include" `
  -I"../external/googletest-1.17.0/googletest" `
  -I"../client" `
  -I"../shared" `
  -lws2_32 -lpthread


  ./ClientTests.exe
```

> **Note:** You will get the warning `warning: #pragma once in main file` Please test to see if the solution runs after you get this warning. 
### Running solution
Navigate to `\pr4j----ATC-system\build\` in 2 terminals

### Running Application (Server)
#### Terminal 1 (Server)
```
cd .\server
python .\Server_UI.py
```
### Running Application (Client)
#### Terminal 2 (Client) option 1 - CMD line
```
cd .\build
.\clientCMD.exe
```
#### Terminal 2 (Client) option 2 - App UI
You can run this by just pressing the play button on the python file. but if you want to use cmd args
```
cd .\client
python .\Client.py
```

---
