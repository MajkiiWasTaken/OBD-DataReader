```
 ██████╗ ██████╗ ██████╗     ██████╗  █████╗ ████████╗ █████╗ 
██╔═══██╗██╔══██╗██╔══██╗    ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
██║   ██║██████╔╝██║  ██║    ██║  ██║███████║   ██║   ███████║
██║   ██║██╔══██╗██║  ██║    ██║  ██║██╔══██║   ██║   ██╔══██║
╚██████╔╝██████╔╝██████╔╝    ██████╔╝██║  ██║   ██║   ██║  ██║
 ╚═════╝ ╚═════╝ ╚═════╝     ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝
```
# ELM327 OBD-II Console Tool

---

##  Overview

Simple Windows C++ console application for reading vehicle diagnostics via an ELM327 adapter.

- No dependencies
- Uses Windows API directly
- Works over COM ports

---

##  Features

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

• Auto-detect COM port  
• Auto-init ELM327  
• Read live vehicle data  
• Show DTC error codes  
• List supported PIDs  
• Live dashboard mode  

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

---

##  Supported Data

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

• RPM  
• Speed  
• Coolant Temperature  
• Intake Air Temperature  
• Engine Load  
• Throttle Position  
• MAF  
• Voltage  
• DTC Codes  

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

---

##  Requirements

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

• Windows  
• C++ compiler (Visual Studio recommended)  
• ELM327 adapter (USB preferred)  
• OBD-II compatible vehicle  
• Ignition ON  

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

---

##  Usage

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

1. Plug in ELM327  
2. Turn ignition ON  
3. Run program  
4. Wait for detection  
5. Select option from menu  

Press **Q** to exit dashboard  

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

---

## Notes

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

• Not all vehicles support all PIDs  
• Cheap adapters may behave inconsistently  
• Bluetooth adapters can be unreliable  

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
